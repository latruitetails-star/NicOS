.intel_syntax noprefix

.section .text
.global _start

_start:
    mov rax, 1
    mov rdi, 1
    lea rsi, [rip + message]
    mov rdx, 11
    int 0x80

hang:
    jmp hang

.section .rodata
message:
    .ascii "Hello Nico\n"
