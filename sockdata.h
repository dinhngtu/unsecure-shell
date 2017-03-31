#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef enum unsh_sockettype {
    SOCKETTYPE_NONE,
    SOCKETTYPE_SERVER,
    SOCKETTYPE_CLIENT,
    SOCKETTYPE_PROC_IN,
    SOCKETTYPE_PROC_OUT
} unsh_sockettype;

typedef enum unsh_sockaff_client_state {
    CLIENTSTATE_UNKNOWN,
    CLIENTSTATE_COMMAND,
    CLIENTSTATE_INPUT
} unsh_sockaff_client_state;

typedef struct unsh_sockaff_client {
    unsh_sockaff_client_state state;
    char *linebuf;
    size_t linelen;
    int childpipe[2];
} unsh_sockaff_client;

typedef struct unsh_sockaff_proc_out {
    int clientfd;
} unsh_sockaff_proc_out;

typedef struct unsh_socket {
    int fd;
    unsh_sockettype socktype;
    union {
        unsh_sockaff_client client;
        unsh_sockaff_proc_out proc_out;
    } sockaff;
} unsh_socket;

unsh_socket *newsock(int fd, unsh_sockettype socktype, bool initialize);