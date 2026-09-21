#ifndef NICOS_SYSCALL_H
#define NICOS_SYSCALL_H

#include <stdint.h>

#define SYS_EXIT    0
#define SYS_WRITE   1
#define SYS_READ    2
#define SYS_OPEN    3
#define SYS_CLOSE   4
#define SYS_MALLOC  5
#define SYS_FREE    6
#define SYS_TIME    7
#define SYS_WINDOW_CREATE 8
#define SYS_WINDOW_TEXT   9








 
typedef struct {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;

    uint64_t rbp;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rbx;
    uint64_t rax;
} SyscallFrame;

uint64_t syscall_dispatch(SyscallFrame *frame);

#endif

typedef struct BootInfo BootInfo;

void syscall_set_boot_info(BootInfo *boot);
void terminal_char(BootInfo *boot, char c);
