#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>

/*
 * A chat server base on EPOLL,
 * The client information are stored in double list
 * More information, please see: http://www.cnblogs.com/ok-wolf/p/7808317.html
 */
 
#define MAX_LISTEN_NUM 1 
#define MAX_EPOLL_NUM 1
#define SERVER_HOST "127.0.0.1"
#define SERVER_PORT 8092
#define MAX_BUF_SIZE 128

#define STR_WELCOME "Welcome to Our Chat! You ID is: Client #%d"
#define STR_MESSAGE "Client #%d>> %s"
#define STR_NOONE_CONNECTED "No one connected to server except you!"
#define CMD_EXIT "EXIT"

#define CHK(eval) if(eval < 0) {perror("eval"); exit(-1);}
#define CHK2(res, eval) if((res = eval) < 0) {perror("eval"); exit(-1);}

#define EPOLL_ET_MODE 1

/*
typedef union epoll_data 
{
   void        *ptr;
   int          fd;
   uint32_t     u32;
   uint64_t     u64;
} epoll_data_t;

struct epoll_event 
{
   uint32_t     events;      //Epoll events  
   epoll_data_t data;        //User data variable 
};
*/

struct double_list
{
	int fd;
	struct double_list *prev;
	struct double_list *next;
};

int handle_message(struct double_list **head, struct double_list **tail, int fd)
{
	char buf[MAX_BUF_SIZE*2], message[MAX_BUF_SIZE];
    memset(buf, 0, MAX_BUF_SIZE);  
	bzero(message, MAX_BUF_SIZE);
	int done = 0;
	ssize_t rc = 0, nread = 0;
	while(done == 0)
	{
    	rc = recv(fd, buf+nread, MAX_BUF_SIZE, 0);  
		if(rc < 0) //rc == -1
		{
			//The fd is non-blocking, if errno == EAGAIN, indicate that no more data to read
			if(errno == EAGAIN)
				break; //no more data to read
			else if(errno == EINTR)
				continue; //interrupt by signal, continue...
			else
				return -1; //read failed
		}
    	else if(rc == 0)
	    {
	    	//client close 
			struct double_list *tmp = *head;
			while(tmp != NULL)
			{
				if(tmp->fd == fd)
				{
					//find it, delete it from the list
					if(tmp == *head)
					{
						//remove the head
						if((*head)->next != NULL)
						{
							*head = (*head)->next;
							if(*head == *tail)
								(*tail)->prev = NULL;
					
							free(tmp);
						}
						else
						{
							free(*head);
							*head = NULL;
							*tail = NULL;
						}
					}
					else if(tmp == *tail)
					{
						//remove the tail
						if((*tail)->prev != NULL)
						{
							*tail = (*tail)->prev;
							if(*tail == *head)
								(*head)->next = NULL;
							free(tmp);
						}
						else
						{
							free(*tail);
							*tail = NULL;
							*head = NULL;
						}	
					}
					else
					{
						//remove a normal one item(not head and not tail)
						tmp->prev->next = tmp->next;
						tmp->next->prev = tmp->prev;
						free(tmp);
					}

					break;
				}
				tmp = tmp->next;
			}

	    	close(fd);
			return 0;
	    }
		else
		{
			nread += rc;
			if(MAX_BUF_SIZE == rc)
			{
				done = 0; //maybe there have data
				if(nread+MAX_BUF_SIZE > sizeof(buf))
				{
					printf("Warning: No more space to recv the message, discard some data\n");
					break; //no more space to put the data, Just discard remainder data 
				}
			}
			else
			{
				done = 1; //read all data
			}
		}
	}

	//Resend the message to other client
	if(*head != NULL && (*head)->next == NULL)
	{
		send(fd, STR_NOONE_CONNECTED, strlen(STR_NOONE_CONNECTED), 0);
		return rc;
	}

	struct double_list *tmp = NULL;
	for(tmp=*head; tmp != NULL; tmp=tmp->next)
	{
		if(tmp->fd != fd)
		{
			bzero(message, MAX_BUF_SIZE);
			snprintf(message, MAX_BUF_SIZE, STR_MESSAGE, fd, buf);
			int n = strlen(message);
			while(n > 0)
			{
				rc = send(tmp->fd, message+strlen(message)-n, n, 0);
				if(rc <= 0)
				{
					if(rc < 0 && errno == EINTR)
						rc = 0; //interrupt by signal
					else
						return -1; //send failed!
				}

				n -= rc;
			}
		}
	}	
	
	
    return rc;  
}

void set_nonblock(int fd)
{
	int fl = fcntl(fd, F_GETFL);
	fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

int main(int argc, char *argv[])
{
	struct sockaddr_in client_addr, server_addr;
	socklen_t client_addr_len = sizeof(client_addr);
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(SERVER_PORT);
	server_addr.sin_addr.s_addr = inet_addr(SERVER_HOST); //htonl(INADDR_ANY);
		
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	assert(fd != 0);
	set_nonblock(fd);
	
	int opt = 1;  
  	setsockopt(fd , SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)); 
	
	int rc = bind(fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
	assert(rc != -1);

	rc = listen(fd, MAX_LISTEN_NUM);
	assert(rc != -1);
	
	struct epoll_event ev, *p_events = NULL;
	int efd = epoll_create(MAX_EPOLL_NUM);
	assert(efd != -1);

	memset(&ev, sizeof(ev), 0);
	ev.data.fd = fd; //don't forget this!
	//EPOLLERR and EPOLLHUP are default added
#ifdef EPOLL_ET_MODE
	ev.events = EPOLLIN | EPOLLET;
#else
	ev.events = EPOLLIN;
#endif
	rc = epoll_ctl(efd, EPOLL_CTL_ADD, fd, &ev);
	assert(rc != -1);

	p_events = (struct epoll_event *)malloc(MAX_EPOLL_NUM * sizeof(struct epoll_event));
	assert(p_events != NULL);
	memset(p_events, 0, MAX_EPOLL_NUM * sizeof(struct epoll_event));

	struct double_list *head = NULL;
	struct double_list *tail = NULL;

	char message[MAX_BUF_SIZE];
	
	while(1)
	{
		int nfds = epoll_wait(efd, p_events, MAX_EPOLL_NUM, -1);
		if(nfds < 0)
		{
			perror("epoll_wait");
			close(fd);
			close(efd);
			return -1;
		}
		
		int i, conn_fd;
		for(i = 0; i < nfds; i++)
		{
			if(p_events[i].data.fd == fd) //new connect is come in, accept it
			{
				while((conn_fd = accept(fd, (struct sockaddr *)&client_addr, &client_addr_len)) > 0)
				{
					bzero(message, MAX_BUF_SIZE);
					sprintf(message, STR_WELCOME, conn_fd);
					rc = send(conn_fd, message, strlen(message), 0);

                    ev.data.fd = conn_fd;
					ev.events = EPOLLIN;
					rc = epoll_ctl(efd, EPOLL_CTL_ADD, conn_fd, &ev);
					assert(rc != -1);
                    
					if(head == NULL)
					{
						head = (struct double_list *)malloc(sizeof(struct double_list));
						assert(head != NULL);
						head->fd = conn_fd;
						head->next = head->prev = NULL;
					}
					else
					{
						struct double_list *tmp = head;
						if(tmp->next == NULL)
						{
							//it is the secondary item, as the tail 
							tail = (struct double_list *)malloc(sizeof(struct double_list));
							assert(tail != NULL);
							tail->fd = conn_fd;
							head->next = tail;
							tail->prev = head;
							tail->next = NULL;
						}
						else
						{
							struct double_list *list = (struct double_list *)malloc(sizeof(struct double_list));
							assert(list != NULL);
							list->fd = conn_fd;
							list->next = list->prev = NULL;
							assert(tail != NULL);
							tail->next = list;
							list->prev = tail;
							tail = list;
						}
					}
				}

				if(conn_fd == -1)
				{
					if(errno != EAGAIN && errno != ECONNABORTED 
						&& errno != EPROTO && errno != EINTR)
					{
						perror("accept");
						return -1;
					}
					continue; //should not return here, since maybe we have handle all fd
				}
			}
            else if(p_events[i].events & EPOLLERR || p_events[i].events & EPOLLHUP)
			{
			    //happen error, delete it 
				rc = epoll_ctl(efd, EPOLL_CTL_DEL, p_events[i].data.fd, &ev);
				assert(rc != -1);
				close(p_events[i].data.fd);
				p_events[i].data.fd = -1;
			}
			else
			{
			    //After accept, we can receive msg from clinet and resend back to other clients.
				handle_message(&head, &tail, p_events[i].data.fd);
			}

			#if 0
			if(p_events[i].events & EPOLLIN)
			{
				//receive data

				ev.data.fd = p_events[i].data.fd;
				ev.events |= EPOLLOUT;
				rc = epoll_ctl(efd, EPOLL_CTL_MOD, p_events[i].data.fd, &ev);
				assert(rc != -1);
			}

			if(p_events[i].events & EPOLLOUT)
			{
				//write data
            
				ev.data.fd = p_events[i].data.fd;
				ev.events |= EPOLLIN;
				rc = epoll_ctl(efd, EPOLL_CTL_MOD, p_events[i].data.fd, &ev);
				assert(rc != -1);
			}
			#endif
		}
	}

	close(fd);
	close(efd);
	return 0;
}

