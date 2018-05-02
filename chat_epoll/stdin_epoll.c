#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <errno.h>
#include <assert.h>

/* 
 * EPOLL test with stdin/stdout
 */
 
#define EPOLL_ET_MODE_TEST 1
//#define STDIN_EPOLL_TEST 1

int stdin_epoll_test()
{
    struct epoll_event ev, events[5];
    bzero(&ev, sizeof(ev));
#ifdef EPOLL_ET_MODE_TEST
    ev.events = EPOLLIN | EPOLLET;
#else
    ev.events = EPOLLIN;
    char buf[64] = {0};
#endif
    ev.data.fd = STDIN_FILENO;
    int efd = epoll_create(1);
    assert(efd != -1);
    int i, nfds = -1;
    int rc = epoll_ctl(efd, EPOLL_CTL_ADD, STDIN_FILENO, &ev);
    assert(rc != -1);
    
    while(1)
    {
        nfds = epoll_wait(efd, events, 5, -1);
        assert(nfds != -1);
        for(i=0; i<nfds; i++)
        {
            if(events[i].data.fd == STDIN_FILENO)
            {
                #ifndef EPOLL_ET_MODE_TEST
                read(STDIN_FILENO, buf, sizeof(buf)); //read all the message
                #endif
                printf("Hello World\n");
                #ifdef EPOLL_ET_MODE_TEST
                ev.data.fd = STDIN_FILENO;
                ev.events = EPOLLIN | EPOLLET;
                rc = epoll_ctl(efd, EPOLL_CTL_MOD, STDIN_FILENO, &ev);
                //EPOLL_CTL_ADD do nothing here, since STDIN_FILE is already in ev,
                //the rc will equal to -1.
                //rc = epoll_ctl(efd, EPOLL_CTL_ADD, STDIN_FILENO, &ev); 
                assert(rc != -1);
                #endif
            }
        }
    }

    close(efd);
    return 0;
}

int stdout_epoll_test()
{
    struct epoll_event ev, events[5];
    bzero(&ev, sizeof(ev));
#ifdef EPOLL_ET_MODE_TEST
    ev.events = EPOLLOUT | EPOLLET;
#else
    ev.events = EPOLLOUT;
#endif
    ev.data.fd = STDOUT_FILENO;
    int nfds, rc, i, efd;
    efd = epoll_create(1);
    assert(efd != -1);
    rc = epoll_ctl(efd, EPOLL_CTL_ADD, STDOUT_FILENO, &ev);
    assert(rc != -1);

    while(1) {
        nfds = epoll_wait(efd, events, 5, -1);
        assert(nfds != -1);

        for(i=0; i<nfds; i++) {
            if(events[i].data.fd == STDOUT_FILENO) {
                //printf("Hello World\n");
                printf("Hello World");
                #ifdef EPOLL_ET_MODE_TEST
                ev.data.fd = STDOUT_FILENO;
                ev.events = EPOLLOUT | EPOLLET;
                rc = epoll_ctl(efd, EPOLL_CTL_MOD, STDOUT_FILENO, &ev);
                //EPOLL_CTL_ADD do nothing here, since STDIN_FILE is already in ev,
                //the rc will equal to -1.
                //rc = epoll_ctl(efd, EPOLL_CTL_ADD, STDIN_FILENO, &ev); 
                assert(rc != -1);
                #endif
            }
        }
    }
    
}

int main(int argc, char *argv[])
{
#ifdef STDIN_EPOLL_TEST
    stdin_epoll_test();
#else
    stdout_epoll_test();
#endif

    return 0;
}
