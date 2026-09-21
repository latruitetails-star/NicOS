.section .text.entry
.global _start
.type _start, @function

_start:
    call main

    mov %rax, %rdi
    call nicos_exit

1:
    hlt
    jmp 1b

.size _start, .-_start
