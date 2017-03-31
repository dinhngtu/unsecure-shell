#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>

#include "config.h"
#include "sockdata.h"

unsh_socket *newsock(int fd, unsh_sockettype socktype, bool initialize) {
    unsh_socket *ret = calloc(1, sizeof(unsh_socket));
    ret->fd = fd;
    ret->socktype = socktype;
    if (initialize) {
        switch (socktype) {
            case SOCKETTYPE_CLIENT:
                ret->sockaff.client.state = CLIENTSTATE_COMMAND;
                ret->sockaff.client.linebuf = malloc(UNSH_LINE_MAX + 1);
                ret->sockaff.client.linelen = 0;
				ret->sockaff.client.childpipe[0] = -1;
				ret->sockaff.client.childpipe[1] = -1;
                break;
            case SOCKETTYPE_PROC_OUT:
				ret->sockaff.proc_out.clientfd = -1;
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