#define _GNU_SOURCE

#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "config.h"

int main(int argc, char **argv) {
    size_t namesize;
    char *name = NULL;

    if (argc == 1) {
        printf("Enter hostname: ");
        int nchar = getline(&name, &namesize, stdin);
        if (nchar < 1) {
            fprintf(stderr, "Bad name\n");
            return 1;
        }
        name[nchar - 1] = 0; // erase the \n
    } else {
        name = argv[1];
    }

    struct hostent *he = gethostbyname(name);
    if (!he) {
        printf("no such domain\n");
        return 2;
    }

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(struct sockaddr_in));
    sa.sin_family = AF_INET;
    memcpy((char *)&sa.sin_addr.s_addr, he->h_addr_list[0], he->h_length);
    sa.sin_port = htons(UNSH_PORT);

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("error creating sockfd");
        return 1;
    }

    if (connect(sockfd, (struct sockaddr *)&sa, sizeof(struct sockaddr_in)) < 0) {
        perror("error connecting");
        return 1;
    }

    int flags = fcntl(sockfd, F_GETFD);
    if (flags < 0) {
        perror("error getting socket state");
        return 1;
    }
    if (fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) < 0) {
        perror("error setting socket state");
        return 1;
    }
    flags = fcntl(0, F_GETFD);
    if (flags < 0) {
        perror("error getting stdin state");
        return 1;
    }
    if (fcntl(0, F_SETFL, flags | O_NONBLOCK) < 0) {
        perror("error setting stdin state");
        return 1;
    }

    int epollfd = epoll_create1(0);
    if (epollfd < 0) {
        perror("error creating epoll socket");
        return 1;
    }

    struct epoll_event *events = malloc(UNSH_MAXEVENTS * sizeof(struct epoll_event));

    struct epoll_event sockopts = {0};
    sockopts.events = EPOLLIN;
    sockopts.data.fd = sockfd;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, sockfd, &sockopts) < 0) {
        perror("cannot set socket epoll");
        return 1;
    }
    memset(&sockopts, 0, sizeof(struct epoll_event));
    sockopts.events = EPOLLIN;
    sockopts.data.fd = 0;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, 0, &sockopts) < 0) {
        perror("cannot set stdin epoll");
        return 1;
    }

    char *buf = malloc(UNSH_BUFSIZE);

    while (1) {
        int pending = epoll_wait(epollfd, events, UNSH_MAXEVENTS, -1);
        if (pending < 0) {
            perror("error waiting for new events");
            return 1;
        }

        for (int i = 0; i < pending; i++) {
            uint32_t evcode = events[i].events;
            int fd = events[i].data.fd;

            if (evcode & EPOLLERR) {
                int sockerr;
                size_t sockerrsize = sizeof(int);
                if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &sockerr, (socklen_t *)&sockerrsize) == 0) {
                    // just quit here because we don't need to serve clients after
                    error(1, sockerr, "fd error");
                }

            } else if (evcode & EPOLLHUP || evcode & EPOLLRDHUP) {
                ssize_t thisread;
                while ((thisread = read(sockfd, buf, UNSH_BUFSIZE)) > 0) {
                    write(1, buf, thisread);
                }
                shutdown(sockfd, SHUT_RDWR);
                close(sockfd);
                return 0;

            } else if (evcode & EPOLLIN) {
                if (fd == 0) {
                    ssize_t thisread;
                    while ((thisread = read(0, buf, UNSH_BUFSIZE)) > 0) {
                        write(sockfd, buf, thisread);
                    }

                } else if (fd == sockfd) {
                    ssize_t thisread;
                    while ((thisread = read(sockfd, buf, UNSH_BUFSIZE)) > 0) {
                        write(1, buf, thisread);
                    }

                } else {
                    fprintf(stderr, "unknown fd");
                    return 1;
                }
            }
        }
    }

    return 0;
}
