#include "net.h"

#include <errno.h>
#include <unistd.h>

int send_all(int fd, const void *buf, size_t len) { /* Writes a full buffer to a socket. */
    const char *p = buf;
    size_t done = 0;
    while (done < len) {
        ssize_t n = write(fd, p + done, len - done);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return -1;
        done += (size_t)n;
    }
    return 0;
}

int recv_all(int fd, void *buf, size_t len) { /* Reads a full buffer from a socket. */
    char *p = buf;
    size_t done = 0;
    while (done < len) {
        ssize_t n = read(fd, p + done, len - done);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return -1;
        done += (size_t)n;
    }
    return 0;
}
