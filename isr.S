.text

.global isr_syscall
.global isr_gp

.extern syscall_dispatch

isr_syscall:
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    subq $8, %rsp
    leaq 8(%rsp), %rdi
    call syscall_dispatch
    movq %rax, 8(%rsp)
    addq $8, %rsp

    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax

    iretq













 
isr_gp:
    mov $0x402, %dx
    mov $'G', %al
    out %al, %dx

    cli

.gp_hang:
    hlt
    jmp .gp_hang
