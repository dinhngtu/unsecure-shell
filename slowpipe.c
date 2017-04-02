#define _GNU_SOURCE

#include <unistd.h>

#define DELAY_USEC 200000

int main() {
    while (1) {
        char dummy;
        ssize_t thisread = read(0, &dummy, 1);
        if (thisread < 0) {
            return 1;
        } else if (thisread == 0) {
            return 0;
        }
        ssize_t thiswrite = write(1, &dummy, 1);
        if (thiswrite < 0) {
            return 1;
        } else if (thiswrite == 0) {
            return 0;
        }
        usleep(DELAY_USEC);
    }
    return 0;
}
