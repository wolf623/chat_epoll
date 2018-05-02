#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <assert.h>
#include <time.h>

#define SERVER_HOST "127.0.0.1"
#define SERVER_PORT 8092
#define MAX_BUF_SIZE 256
#define MAX_CLIENT_NUM 1000

int main(int argc, char *argv[])
{
	struct sockaddr_in serv_addr;
	int serv_addr_len = sizeof(struct sockaddr_in);
	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(SERVER_PORT);
	serv_addr.sin_addr.s_addr = inet_addr(SERVER_HOST); //htonl(INADDR_ANY);

	int i, rc, fd, fds[MAX_CLIENT_NUM];
	char message[MAX_BUF_SIZE] = {0};
	clock_t start_time = clock();
	for(i=0; i<MAX_CLIENT_NUM; i++)
	{
		fd = socket(AF_INET, SOCK_STREAM, 0);
		assert(fd != -1);

		rc = connect(fd, (struct sockaddr *)&serv_addr, serv_addr_len);
		assert(rc != -1);
		fds[i] = fd;

		bzero(message, MAX_BUF_SIZE);
		rc = recv(fd, message, MAX_BUF_SIZE, 0);
		printf("%s\n", message);
	}
	
	for(i=0; i<MAX_CLIENT_NUM; i++)
	{
		close(fds[i]);
	}
	printf("Total connections: %d, Test passed at: %.2f seconds\n", MAX_CLIENT_NUM, (double)(clock()-start_time)/CLOCKS_PER_SEC);
	
	
	return 0;
}

