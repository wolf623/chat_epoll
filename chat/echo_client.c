#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <assert.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <errno.h>
#include <sys/epoll.h>

/*
 * A echo client
 * More informatin, please see: http://www.cnblogs.com/ok-wolf/p/7808317.html
 */
 
#define MAX_MSG_LEN 64
#define SERVER_HOST "127.0.0.1"
#define SERVER_PORT 8092

#define CHK(eval) if(eval < 0) {perror("eval"); exit(-1);}
#define CHK2(res, eval) if((res = eval) < 0) {perror("eval"); exit(-1);}

int main(int argc, char **argv[])
{
    int efd, nfds, rc, i, fd;
    char buf[MAX_MSG_LEN] = {0};
    struct sockaddr_in server_addr;
    bzero(&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = inet_addr(SERVER_HOST);
    CHK2(fd, socket(AF_INET, SOCK_STREAM, 0));

    CHK2(rc, connect(fd, (struct sockaddr *)&server_addr, sizeof(struct sockaddr_in)));
   
    //read from stdin
    while(1) {
        printf("Enter 'exit' to exit\n");
        
        bzero(&buf, sizeof(buf));
        fgets(buf, sizeof(buf), stdin);
        buf[strlen(buf)-1] = '\0'; //replace the '\n' to '\0'

        //For simple, we do not allow input nothing(in fact, it only has '\n')
        if(strlen(buf) == 0) {
            printf("please input something...\n");
            continue;
        }
        
        if(strncmp(buf, "exit", 4) == 0)
            break;
        
        //send to echo server
        send(fd, buf, strlen(buf), 0);
        //recv from echo and print it
        bzero(&buf, sizeof(buf));
        recv(fd, buf, sizeof(buf), 0);

        printf("%s\n", buf);
    }

    close(fd);
    return 0;
}

