#include <errno.h>
#include <unistd.h>

/*
ssize_t readlineasync(int fd, void *buf, size_t count) {
    size_t totalread = 0;
    char *now = buf;
    while (read < count - 1) {
        ssize_t thisread = read(fd, now, 1);
        if (thisread > 0) {
            now++;
            totalread++;
        } else if (thisread == 0 || *now == '\n') {
            break;
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            goto finish;
        } else {
            return -1;
        }
    }
finish:
    *now = 0;
    return totalread;
}
*/
