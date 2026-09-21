#include "syscall.h"

static BootInfo *syscall_boot = 0;

void syscall_set_boot_info(BootInfo *boot)
{
    syscall_boot = boot;
}

#include "window.h"

static void syscall_debug(const char *s)
{
    while (*s) {
        __asm__ volatile (
            "outb %0, %1"
            :
            : "a"(*s),
              "Nd"((uint16_t)0x402)
        );

        s++;
    }
}

static uint64_t syscall_write(
    uint64_t fd,
    const char *buffer,
    uint64_t length
)
{
    (void)fd;

    if (!syscall_boot || !buffer)
        return (uint64_t)-1;

    for (uint64_t i = 0; i < length; i++) {
        terminal_char(syscall_boot, buffer[i]);
    }

    return length;
}

extern void ring3_exit(uint64_t status);

static uint64_t syscall_exit(uint64_t status)
{
    syscall_debug("[SYSCALL] SYS_EXIT\r\n");
    ring3_exit(status);

    for (;;) {
        __asm__ volatile ("hlt");
    }
    return 0;
}

static uint64_t syscall_window_create(
    uint64_t x,
    uint64_t y,
    uint64_t width,
    uint64_t height
)
{
    syscall_debug("[SYSCALL] SYS_WINDOW_CREATE\r\n");

    return (uint64_t)window_create(
        (uint32_t)x,
        (uint32_t)y,
        (uint32_t)width,
        (uint32_t)height
    );
}

static uint64_t syscall_window_text(
    uint64_t window_id,
    uint64_t x,
    uint64_t y,
    const char *text,
    uint64_t scale,
    uint64_t color
)
{
    syscall_debug("[SYSCALL] SYS_WINDOW_TEXT\r\n");

    if (!text)
        return (uint64_t)-1;

    return (uint64_t)window_draw_text(
        (int)window_id,
        (uint32_t)x,
        (uint32_t)y,
        text,
        (uint32_t)scale,
        (uint32_t)color
    );
}

uint64_t syscall_dispatch(SyscallFrame *frame)
{
    if (!frame)
        return (uint64_t)-1;

    switch (frame->rax) {

        case SYS_EXIT:
            return syscall_exit(frame->rdi);

        case SYS_WRITE:
            return syscall_write(
                frame->rdi,
                (const char *)frame->rsi,
                frame->rdx
            );

        case SYS_WINDOW_CREATE:
            return syscall_window_create(
                frame->rdi,
                frame->rsi,
                frame->rdx,
                frame->rcx
            );

        case SYS_WINDOW_TEXT:
            return syscall_window_text(
                frame->rdi,
                frame->rsi,
                frame->rdx,
                (const char *)frame->r10,
                frame->r8,
                frame->r9
            );

        default:
            syscall_debug("[SYSCALL] UNKNOWN\r\n");
            return (uint64_t)-1;
    }
}
