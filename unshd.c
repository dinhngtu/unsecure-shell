#define _GNU_SOURCE

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "config.h"
#include "readcmd.h"
#include "sockdata.h"

int cmdspawn(int epollfd, unsh_socket *clientsock, struct cmdline *cmd) {
    char ***seq = cmd->seq;

    if (!*(seq)) {
        return 0;
    }

    int cmdcount;
    for (cmdcount = 0; seq[cmdcount]; cmdcount++);

    // in case of io redir
    int redirfd[2] = {-1, -1};
    // pipes for communicating with child processes
    int headpipe[2];
    int tailpipe[2];
    // pipeline chain
    int before[2], after[2];
    bool beginning = true;

    // setup head-of-pipe and tail-of-pipe
    // if redirected to client then these must be non-blocking for use with epoll()
    if (cmd->in) {
        redirfd[0] = open(cmd->in, O_RDONLY);
        if (redirfd[0] < 0) {
            perror("cannot open input file");
            return -1;
        }
    } else {
        if (pipe2(headpipe, O_NONBLOCK) < 0) {
            perror("cannot create head pipe");
            return -1;
        }
        // no need to register head pipe with epoll() since we won't be polling from it
    }

    if (cmd->out) {
        redirfd[1] = open(cmd->out, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (redirfd[1] < 0) {
            perror("cannot open output file");
            return -1;
        }
    } else {
        if (pipe2(tailpipe, O_NONBLOCK) < 0) {
            perror("cannot create tail pipe");
            return -1;
        }

        struct epoll_event tpopts = {0};
        tpopts.events = EPOLLIN | EPOLLRDHUP;
        unsh_socket *tpsock = newsock(tailpipe[0], (unsh_sockettype)SOCKETTYPE_PROC_OUT, true);
        tpsock->sockaff.proc_out.clientsock = clientsock;
        tpopts.data.ptr = tpsock;
        // we only monitor output side of pipe for now
        // monitoring read side of pipe is not implemented yet
        // writes returning EAGAIN may behave badly
        if (epoll_ctl(epollfd, EPOLL_CTL_ADD, tailpipe[0], &tpopts) != 0) {
            perror("cannot register child pipe events");
            return -1;
        }
    }

    clientsock->sockaff.client.haspipe = true;

    while (*seq) {
        char **current = *seq++;

        if (*seq) {
            // not the end of the pipe yet
            if (pipe(after) < 0) {
                perror("cannot create pipe");
            }
        }

        pid_t pid = fork();
        if (!pid) {
            if (beginning) {
                if (cmd->in) {
                    dup2(redirfd[0], 0);
                    close(redirfd[0]);
                } else {
                    dup2(headpipe[0], 0);
                    close(headpipe[0]);
                    close(headpipe[1]);
                }
            } else {
                dup2(before[0], 0);
                close(before[0]);
                close(before[1]);
            }

            if (!*seq) {
                // end of pipe
                if (cmd->out) {
                    dup2(redirfd[1], 1);
                    close(redirfd[1]);
                } else {
                    dup2(tailpipe[1], 1);
                    close(tailpipe[0]);
                    close(tailpipe[1]);
                }
            } else {
                dup2(after[1], 1);
                close(after[0]);
                close(after[1]);
            }

            execvp(current[0], current);
            perror("cannot exec");
            return -1;

        } else if (pid > 0) {
            if (beginning) {
                if (cmd->in) {
                    close(redirfd[0]);
                } else {
                    close(headpipe[0]);
                    // TODO: remove close() if headpipe[1] is ever used
                    close(headpipe[1]);
                }
            }
            if (!*seq) {
                if (cmd->out) {
                    close(redirfd[1]);
                } else {
                    // do not close tailpipe[0] in server since it is our epoll channel to the pipe chain
                    close(tailpipe[1]);
                }
            }
            if (!beginning) {
                close(before[0]);
                close(before[1]);
            }
            before[0] = after[0];
            before[1] = after[1];
            beginning = false;

        } else {
            perror("cannot fork");
            return -1;
        }
    }

    return 0;
}

int handle_client_read(int epollfd, struct epoll_event event) {
    unsh_socket *sockdt = event.data.ptr;
    int fd = sockdt->fd;
    unsh_sockaff_client client = sockdt->sockaff.client;

    if (client.state == CLIENTSTATE_COMMAND) {
        ssize_t thisread;
        char *lineptr = client.linebuf;
        while (client.linelen < UNSH_LINE_MAX && (thisread = read(fd, lineptr, 1)) > 0) {
            char readed = *(char *)lineptr;
            if (readed == '\r' || readed == '\n') {
                *lineptr++ = 0;
                struct cmdline *cmd = readcmd(client.linebuf);
                // luckily for us exec() won't mess up parent's epoll
                if (cmdspawn(epollfd, sockdt, cmd) == -1) {
                    perror("command spawn failed");
                }
                break;
            } else {
                client.linelen++;
                lineptr++;
            }
        }
        if (thisread == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return 0;
        }
        return thisread;

    } else if (client.state == CLIENTSTATE_INPUT) {
        return 0;
        // NOTE: NOT IMPLEMENTED
        ssize_t thisread;
        char *buf = malloc(UNSH_BUFSIZE);
        // read from client socket and pump it to child stdin
        while ((thisread = read(fd, buf, UNSH_BUFSIZE)) > 0) {
            write(client.childpipe[1], buf, thisread);
        }
        if (thisread == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return 0;
        }
        return thisread;

    } else {
        fprintf(stderr, "unknown client state");
        return -1;
    }
}

int handle_proc_out_read(struct epoll_event event) {
    // TODO: use a constrained for loop rather than a while loop to avoid starving other fds
    unsh_socket *sockdt = event.data.ptr;
    assert(sockdt->socktype == SOCKETTYPE_PROC_OUT);

    unsh_socket *clientsock = sockdt->sockaff.proc_out.clientsock;
    assert(clientsock->socktype == SOCKETTYPE_CLIENT);
    if (clientsock->sockaff.client.state == CLIENTSTATE_CLOSED) {
        freesock(clientsock);
        return 0;
    }

    int fd = sockdt->fd;
    ssize_t thisread;
    char *buf = malloc(UNSH_BUFSIZE);
    while ((thisread = read(fd, buf, UNSH_BUFSIZE)) > 0) {
        write(clientsock->fd, buf, thisread);
    }
    if (thisread == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return 0;
    }
    return thisread;
}

int main() {
    sigset_t chs;
    if (sigemptyset(&chs) != 0) {
        perror("cannot initialize signal set");
        return 1;
    }
    if (sigaddset(&chs, SIGCHLD) != 0) {
        perror("cannot initialize signal set");
        return 1;
    }
    int sigfd = signalfd(-1, &chs, SFD_NONBLOCK);
    if (sigfd < 0) {
        perror("error registering signalfd");
        return 1;
    }
    if (sigaddset(&chs, SIGPIPE) != 0) {
        perror("cannot initialize signal set");
        return 1;
    }
    if (sigprocmask(SIG_BLOCK, &chs, NULL) != 0) {
        perror("cannot mask signals");
        return 1;
    }

    int sockfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (sockfd < 0) {
        perror("error creating sockfd");
        return 1;
    }

    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) != 0) {
        perror("error setting SO_REUSEADDR");
        return 1;
    }

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(struct sockaddr_in));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    sa.sin_port = htons(UNSH_PORT);

    if (bind(sockfd, (struct sockaddr *)&sa, sizeof(struct sockaddr_in)) < 0) {
        perror("error binding");
        return 1;
    }

    if (listen(sockfd, 5) < 0) {
        perror("error listening");
        return 1;
    }

    int epollfd = epoll_create1(SOCK_CLOEXEC);
    if (epollfd <= 0) {
        perror("error creating epoll");
        return 1;
    }

    struct epoll_event *events = malloc(UNSH_MAXEVENTS * sizeof(struct epoll_event));

    // register server socket gives us accept() notifications
    struct epoll_event ssopts = {0};
    ssopts.events = EPOLLIN;
    ssopts.data.ptr = newsock(sockfd, (unsh_sockettype)SOCKETTYPE_SERVER, true);
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, sockfd, &ssopts) != 0) {
        perror("cannot set sockfd events");
        return 1;
    }

    while (1) {
        int pending = epoll_wait(epollfd, events, UNSH_MAXEVENTS, -1);
        if (pending < 0) {
            perror("error waiting for new event");
            continue;
        }

        for (int ei = 0; ei < pending; ei++) {
            uint32_t evcode = events[ei].events;
            unsh_socket *sockdt = events[ei].data.ptr;

            if (evcode & EPOLLERR) {
                fprintf(stderr, "oops\n");
                int sockerr;
                size_t sockerrsize = sizeof(int);
                if (getsockopt(sockdt->fd, SOL_SOCKET, SO_ERROR, &sockerr, (socklen_t *)&sockerrsize) == 0) {
                    error(0, sockerr, "fd error");
                }
                if (sockdt->fd == sockfd) {
                    fprintf(stderr, "socket fd encountered unexpected error, quitting\n");
                    return 1;
                }
                epoll_ctl(epollfd, sockdt->fd, EPOLL_CTL_DEL, NULL);
                close(sockdt->fd);
                continue;

            } else if (sockdt->fd == sockfd) {
                while (1) {
                    struct sockaddr_in ca;
                    socklen_t clen = sizeof(struct sockaddr_in);
                    int newfd = accept4(sockfd, (struct sockaddr *)&ca, &clen, SOCK_NONBLOCK);
                    if (newfd < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            // no connections waiting for accept
                            break;
                        } else {
                            perror("error accepting connection");
                        }
                    } else {
                        struct epoll_event copts = {0};
                        copts.events = EPOLLIN | EPOLLRDHUP;
                        copts.data.ptr = newsock(newfd, (unsh_sockettype)SOCKETTYPE_CLIENT, true);
                        if (epoll_ctl(epollfd, EPOLL_CTL_ADD, newfd, &copts) != 0) {
                            perror("cannot set fd events");
                            close(newfd);
                        }
                    }
                }

            } else if (sockdt->fd == sigfd) {
                struct signalfd_siginfo siginfo;
                while (read(sigfd, &siginfo, sizeof(struct signalfd_siginfo)) > 0) {
                    if (siginfo.ssi_signo == SIGCHLD) {
                        while (waitpid(-1, NULL, WNOHANG) > 0);
                    }
                }

            } else if (evcode & EPOLLHUP || evcode & EPOLLRDHUP) {
                switch (sockdt->socktype) {
                    case SOCKETTYPE_CLIENT:
                        if (sockdt->sockaff.client.childpipe[0] >= 0) {
                            // pipeline input is not implemented yet
                            //close(sockdt->sockaff.client.childpipe[0]);
                            //close(sockdt->sockaff.client.childpipe[1]);
                        }
                        sockdt->sockaff.client.state = CLIENTSTATE_CLOSED;
                        epoll_ctl(epollfd, sockdt->fd, EPOLL_CTL_DEL, NULL);
                        close(sockdt->fd);
                        if (!sockdt->sockaff.client.haspipe) {
                            freesock(sockdt);
                        }
                        break;
                    case SOCKETTYPE_PROC_OUT:
                        // pipeline output is done
                        // flush pipeline output to client first
                        sockdt->sockaff.proc_out.clientsock->sockaff.client.haspipe = false;
                        handle_proc_out_read(events[ei]);
                        epoll_ctl(epollfd, sockdt->fd, EPOLL_CTL_DEL, NULL);
                        close(sockdt->fd);
                        freesock(sockdt);
                        break;
                    default:
                        //close(sockdt->fd);
                        break;
                }

            } else if (evcode & EPOLLIN) {
                switch (sockdt->socktype) {
                    case SOCKETTYPE_CLIENT:
                        handle_client_read(epollfd, events[ei]);
                        break;
                    case SOCKETTYPE_PROC_IN:
                        // TODO?
                        break;
                    case SOCKETTYPE_PROC_OUT:
                        handle_proc_out_read(events[ei]);
                        break;
                    default:
                        break;
                }

            } else {
                fprintf(stderr, "unknown event state\n");
                continue;
            }
        }
    }
}
