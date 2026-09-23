#ifndef NET_H
#define NET_H

#include <stddef.h>

int send_all(int fd, const void *buf, size_t len);
int recv_all(int fd, void *buf, size_t len);

#endif
