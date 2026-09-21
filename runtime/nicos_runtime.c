#include "nicos.h"

int nicos_write(int fd, const char *buffer, int length)
{
    long ret;

    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"((long)NICOS_SYS_WRITE),
          "D"((long)fd),
          "S"(buffer),
          "d"((long)length)
        : "rcx", "r11", "memory"
    );

    return (int)ret;
}

void nicos_exit(int status)
{
    __asm__ volatile (
        "int $0x80"
        :
        : "a"((long)NICOS_SYS_EXIT),
          "D"((long)status)
        : "rcx", "r11", "memory"
    );

    for (;;) {
        __asm__ volatile ("hlt");
    }
}
