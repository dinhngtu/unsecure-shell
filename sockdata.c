#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

#include "config.h"
#include "sockdata.h"

static const char *unsh_sockettype_strings[] = {
    "None",
    "Server",
    "Client",
    "Proc-In",
    "Proc-Out"
};

unsh_socket *newsock(int fd, unsh_sockettype socktype, bool initialize) {
	fprintf(stderr, "creating socket type %s\n", unsh_sockettype_strings[socktype]);
    unsh_socket *ret = calloc(1, sizeof(unsh_socket));
    ret->fd = fd;
    ret->socktype = socktype;
    if (initialize) {
        switch (socktype) {
            case SOCKETTYPE_CLIENT:
                ret->sockaff.client.state = CLIENTSTATE_COMMAND;
                ret->sockaff.client.haspipe = false;
                ret->sockaff.client.linebuf = malloc(UNSH_LINE_MAX + 1);
                ret->sockaff.client.linelen = 0;
                ret->sockaff.client.childpipe[0] = -1;
                ret->sockaff.client.childpipe[1] = -1;
                break;
            case SOCKETTYPE_PROC_OUT:
                ret->sockaff.proc_out.clientsock = NULL;
                break;
            default:
                break;
        }
    }
    return ret;
}

void freesock(unsh_socket *sock) {
    if (!sock) {
        return;
    }
	fprintf(stderr, "freeing socket type %s\n", unsh_sockettype_strings[sock->socktype]);
    switch (sock->socktype) {
        case SOCKETTYPE_CLIENT:
            free(sock->sockaff.client.linebuf);
            break;
        case SOCKETTYPE_PROC_OUT:
            break;
        default:
            break;
    }
    free(sock);
}
