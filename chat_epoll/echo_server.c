#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <assert.h>
#include <errno.h>
#include <string.h>

/*
 * This is echo server 
 * Support max client number is 10
 * More information, please see: http://www.cnblogs.com/ok-wolf/p/7808317.html
*/
#define MAX_CLIENT_NUM 10
#define MAX_MSG_LEN 32
#define SERVER_HOST "127.0.0.1"
#define SERVER_PORT 8092

#define CHK(eval) if(eval < 0) {perror("eval"); exit(-1);}
#define CHK2(res, eval) if((res = eval) < 0) {perror("eval"); exit(-1);}

//Default is ET mode, or if you want to use LT mode,
//Then below select one from two choices
//#define EPOLL_LT_NONBLOCK_MODE 1
//#define EPOLL_LT_BLOCK 1
#define EPOLL_QUICK_SEND

void set_sock_addr_reuse(int fd)
{
    int opt = 1;  
    setsockopt(fd , SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)); 
}

void set_nonblock(int fd)
{
    int fl = fcntl(fd, F_GETFL);
    assert(fl != -1);
    int rc = fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    assert(rc != -1);
}

/*
 * Return Value: data len that have read
 * Error: -1: read failed, -2: peer fd is closed, -3: no more space
 */
int sock_recv(int fd, char *ptr, int len)
{
    assert(len > 0 && fd > 0);
    assert(ptr != NULL);
    int nread = 0, n = 0;
    while(1) {
        nread = read(fd, ptr+n, len-1);
        if(nread < 0) {
            if(errno == EAGAIN || errno == EWOULDBLOCK) {
                return nread; //have read one
            } else if(errno == EINTR) {
                continue; //interrupt by signal, continue
            } else if(errno == ECONNRESET) {
                return -1; //client send RST
            } else {
                return -1; //faild
            }
        } else if(nread == 0) {
            return -2; //client is closed
        } else if(nread < len-1) {
            return nread; //no more data, read done
        } else {
            /*
             * Here, if nread == len-1, maybe have add done,
             * For simple, we just return here,
             * A better way is to MOD EPOLLIN into epoll events again
             */
            return -3; //no more space
        }
    }

    return nread;
}

/*
 * Return Value: data len that can not send out
 * Normal Value: 0, Error Value: -1
 */
int sock_send(int fd, char *ptr, int len)
{
    assert(fd > 0);
    assert(ptr != NULL);
    assert(len > 0);
    int nsend = 0, n = len;
    while(n > 0) {
        nsend = send(fd, ptr+len-n, n, 0);
        if(nsend < 0) {
            if(errno == EINTR) {
				nsend = 0; //interrupt by signal
			} else if(errno == EAGAIN) {
                //Here, write buffer is full, for simple, just sleep,
                //A better is add EPOLLOUT again?
                usleep(1); 
                continue;
			} else {
				return -1; //send failed!
            }
        }

        if(nsend == n) {
            return 0; //send all data
        }
        
        n -= nsend;
    }

    return n;
}

int main(int argc, char *argv[])
{
    int efd, nfds, rc, i, fd, conn_fd;
    char buf[MAX_MSG_LEN] = {0};
    struct sockaddr_in client_addr, server_addr;
    bzero(&server_addr, sizeof(server_addr));
    socklen_t addr_len = sizeof(client_addr);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = inet_addr(SERVER_HOST);
    
    CHK2(fd, socket(AF_INET, SOCK_STREAM, 0));
    
#ifndef EPOLL_LT_BLOCK
    set_nonblock(fd);
#endif
	
    set_sock_addr_reuse(fd);
    CHK2(rc, bind(fd, (struct sockaddr *)&server_addr, sizeof(server_addr)));
    CHK2(rc, listen(fd, MAX_CLIENT_NUM));

    //Epoll Coding Here
    efd = epoll_create(MAX_CLIENT_NUM);
    if(efd == -1) {
        perror("epoll_create");
        close(fd);
        exit(-1);
    }
    struct epoll_event ev, events[MAX_CLIENT_NUM];
    bzero(&ev, sizeof(ev));
    ev.events = EPOLLIN | EPOLLOUT;
    ev.data.fd = fd; 
    rc = epoll_ctl(efd, EPOLL_CTL_ADD, fd, &ev);
    if(rc == -1) {
        perror("epoll_ctl");
        close(efd);
        close(fd);
        exit(-1);
    }

    while(1) {
        nfds = epoll_wait(efd, events, MAX_CLIENT_NUM, -1);
        if(nfds < 0) {
            close(efd);
            close(fd);
            exit(-1);
        }

        for(i=0; i<nfds; i++) {
            if(events[i].data.fd == fd) {
#ifndef EPOLL_LT_BLOCK
                while((conn_fd = accept(fd, (struct sockaddr *)&client_addr, &addr_len)) != -1) {
#else
                if((conn_fd = accept(fd, (struct sockaddr *)&client_addr, &addr_len)) != -1) {
#endif
                    
#ifndef EPOLL_LT_BLOCK
                    set_nonblock(conn_fd);
#endif
                    /*
                     * After accept, the server can send msg to client right now
                     * if there is a need, but here, we need to wait to receive msg from client,
                     * maybe the msg is on the way...
                     */
                    
#if((defined EPOLL_LT_BLOCK) || (defined EPOLL_LT_NONBLOCK_MODE))  
                    ev.events = EPOLLIN;
#else
                    ev.events = EPOLLIN | EPOLLET; //default we use ET mode
#endif
                    //Add the conn_fd into epoll events
                    ev.data.fd = conn_fd;
                    rc = epoll_ctl(efd, EPOLL_CTL_ADD, conn_fd, &ev);
                    assert(rc != -1);
                }

                if(conn_fd == -1) {
                    if(errno != EAGAIN && errno != ECONNABORTED 
						&& errno != EPROTO && errno != EINTR) {
						perror("accept");
						exit(-1);
					}
                }
            }
            else if(events[i].events & EPOLLIN) {
                bzero(&buf, sizeof(buf));
                
#if((defined EPOLL_LT_BLOCK) || (defined EPOLL_LT_NONBLOCK_MODE))  
                rc = read(events[i].data.fd, buf, sizeof(buf));
                if(rc == 0) {
                    //client is closed, so delete it from epoll events
                    ev.data.fd = events[i].data.fd;
                    ev.events = EPOLLOUT; 
                    rc = epoll_ctl(efd, EPOLL_CTL_DEL, ev.data.fd, &ev);
                    assert(rc != -1);
                    close(ev.data.fd);
                    continue;
                }
#else
                //Under ET mode, we should do special operation
                rc = sock_recv(events[i].data.fd, buf, sizeof(buf));
                if(rc == -1) {
                    continue; //read failed
                } else if(rc == -2) {
                    //client is closed, delete it 
                    ev.data.fd = events[i].data.fd;
                    //ev.events = EPOLLOUT; //This is ignored
                    rc = epoll_ctl(efd, EPOLL_CTL_DEL, ev.data.fd, &ev);
                    assert(rc != -1);
                    close(ev.data.fd);
                    continue;
                } else if(rc == -3) {
                    //Give a warning msg to client
                    char *str = "Warning: message is too long!!!";
                    snprintf(buf, strlen(str), "%s", str);
                    buf[strlen(str)] = '\0';
                }
                    
#endif
                //We have received msg from client, then we can send it back to client now
//#if((defined EPOLL_LT_BLOCK) || (defined EPOLL_LT_NONBLOCK_MODE))
#ifndef EPOLL_QUICK_SEND
                ev.data.fd = events[i].data.fd;
                ev.events = EPOLLOUT;
                rc = epoll_ctl(efd, EPOLL_CTL_MOD, ev.data.fd, &ev);
                assert(rc != -1);
#else
                sock_send(events[i].data.fd, buf, strlen(buf));
#endif
            }
            else if(events[i].events & EPOLLOUT) {
#if((defined EPOLL_LT_BLOCK) || (defined EPOLL_LT_NONBLOCK_MODE))
                send(events[i].data.fd, buf, strlen(buf), 0);
#else
                sock_send(events[i].data.fd, buf, strlen(buf));
#endif
                //send msg out, mode EPOLLIN again?
                ev.data.fd = events[i].data.fd;
                ev.events = EPOLLIN;
                rc = epoll_ctl(efd, EPOLL_CTL_MOD, ev.data.fd, &ev);
                assert(rc != -1);
            }
            else {
                //happen error, delete it from epoll events
                ev.data.fd = events[i].data.fd;
                //ev.events = EPOLLOUT; //This is ignored
                rc = epoll_ctl(efd, EPOLL_CTL_DEL, ev.data.fd, &ev);
                assert(rc != -1);
                close(ev.data.fd);
            }
        }
    }

    printf("ByeBye...\n");
    close(efd);
    return 0;
}

