#ifndef NICOS_H
#define NICOS_H

#include <stdint.h>

#define NICOS_STDIN   0
#define NICOS_STDOUT  1
#define NICOS_STDERR  2

#define NICOS_SYS_EXIT   0
#define NICOS_SYS_WRITE  1
#define NICOS_SYS_READ   2

int nicos_write(int fd, const char *buffer, int length);
void nicos_exit(int status);

#endif
