#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <sys/epoll.h>


/* 
 * A chat client base on EPOLL
 * The child process function: wait for user input and send them to parent process,
 * The parent process function: send msg to char server and receive msg from char server,
 * The connection between child and parent process is based on PIPE. 
 * More inforamtion, please see: http://www.cnblogs.com/ok-wolf/p/7808317.html
 */
 
#define SERVER_HOST "127.0.0.1"
#define SERVER_PORT 8092
#define MAX_BUF_SIZE 20
#define MAX_EPOLL_NUM 2
#define CMD_EXIT "exit"

#define CHK(eval) if(eval < 0){perror("eval"); exit(-1);}
#define CHK2(res, eval) if((res = eval) < 0){perror("eval"); exit(-1);}

#define EPOLL_ET_MODE 1


void set_nonblock(int fd)
{
	int fl = fcntl(fd, F_GETFL);
	fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

int main(int argc, char *argv[])
{	
	char message[MAX_BUF_SIZE*2];
	struct sockaddr_in serv_addr;
	bzero(&serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(SERVER_PORT);
	serv_addr.sin_addr.s_addr = inet_addr(SERVER_HOST);

	int fd = 0;
	CHK2(fd, socket(AF_INET, SOCK_STREAM, 0));
	
	int rc = 0;
	CHK2(rc, connect(fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)));

	set_nonblock(fd); 
	
	int pipe_fd[2]; //pipe_fd[0]: read, pipe_fd[1]: write
	CHK(pipe(pipe_fd));

	struct epoll_event ev, events[MAX_EPOLL_NUM];
#ifdef EPOLL_ET_MODE
	ev.events = EPOLLIN | EPOLLET; //ET mode
#else
	ev.events = EPOLLIN;
#endif
	int efd;
	CHK2(efd, epoll_create(MAX_EPOLL_NUM));

	ev.data.fd = fd;
	CHK2(rc, epoll_ctl(efd, EPOLL_CTL_ADD, fd, &ev));
	ev.data.fd = pipe_fd[0];
	CHK2(rc, epoll_ctl(efd, EPOLL_CTL_ADD, pipe_fd[0], &ev))

	int exit_flag = 0;
	CHK2(rc, fork());

	if(rc < 0)
	{
		perror("fork");
		close(fd);
		close(pipe_fd[0]);
		close(pipe_fd[1]);
		close(efd);
	}
	else if(rc == 0)
	{
		//child recv message from input and pass it to parent to send to server
		close(pipe_fd[0]); //close read fd
		while(exit_flag == 0)
		{	
			printf("Enter 'exit' to exit\n");
			bzero(message, sizeof(message));
			fgets(message, sizeof(message), stdin);
			message[strlen(message)-1] = '\0';
            
			if(strncmp(message, CMD_EXIT, strlen(CMD_EXIT)) == 0)
				exit_flag = 1;
			else
				CHK(write(pipe_fd[1], message, strlen(message))); //pass it to parent
		}
	}
	else
	{
		//parent recv message from server and print it
		close(pipe_fd[1]); //close write fd
		int i, n, has_data_flag, nread, nfds = 0;
		while(exit_flag == 0)
		{
			CHK2(nfds, epoll_wait(efd, events, MAX_EPOLL_NUM, -1));

			for(i=0; i<nfds; i++)
			{
				bzero(message, sizeof(message));
				nread = 0; //reset it
				n = 0; //reset it
				has_data_flag = 0;
				if(events[i].data.fd == fd)
				{
					while(1)
					{
						rc = recv(fd, message+nread, MAX_BUF_SIZE, 0);
						if(rc < 0)
						{
							//The fd is non-blocking, if errno == EAGAIN, indicate that no more data to read
							if(errno == EAGAIN)
                            {
                                if(has_data_flag == 1)
                                    printf("%s\n", message); //print out what we have received
								break; //no more data to read
                            }
							else if(errno == EINTR)
                            {                     
								continue; //interrupt by signal, continue...
                            }
							else
                            {                     
								return -1; //read failed
                            }
						}
						else if(rc == 0)
						{
							//server is closed
							exit_flag = 1; //let child exit
							break;
						}
						else
						{
							//a normal message, then show it
							nread += rc;
							if(MAX_BUF_SIZE == rc)
							{
								if(nread+MAX_BUF_SIZE > sizeof(message))
								{
									printf("Warning: No more space to recv the message, discard the message.\n");
									break; //no more space to put the data, Just discard remainder data 
								}
                                has_data_flag = 1; //we have receive some data.
								continue;
							}
							else
							{
								printf("%s\n", message);
								break;
							}
						}
					}
				}
				else if(events[i].data.fd == pipe_fd[0])
				{
					//it is from child, receive the message
					CHK2(rc, read(pipe_fd[0], message, sizeof(message)));
					if(rc == 0)
					{
						//happen error
						exit_flag = 1; //let child exit
					}
					else
					{
						//a normal message inputed, send it to server
						//CHK2(rc, send(fd, message, strlen(message), 0));
						n = strlen(message);
						while(n > 0)
						{
							rc = send(fd, message+strlen(message)-n, n, 0);
							if(rc <= 0)
							{
								if(rc < 0 && errno == EINTR)
									rc = 0; //interrupt by signal, continue...
								else
									return -1; //send failed
							}

							n -= rc;
						}
					}
				}
			}
		}
	}

	if(rc == 0)
	{
		//child
		close(pipe_fd[1]); //close write fd
	}
	else
	{
		//parent
		close(pipe_fd[0]); //close read fd
		close(fd);
	}

	close(efd);

	printf("ByeBye...\n");
	return 0;
}

