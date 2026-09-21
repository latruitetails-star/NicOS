.intel_syntax noprefix

.section .data
.global kernel_ring3_rsp
kernel_ring3_rsp:
    .quad 0

.section .text

# uint64_t ring3_enter(uint64_t entry, uint64_t stack);
# RDI = entry (USER_BASE)
# RSI = stack (USER_STACK_TOP)
.global ring3_enter
ring3_enter:
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15

    mov [rip + kernel_ring3_rsp], rsp

    push 0x23      # SS (User Data)
    push rsi       # RSP (User Stack)
    push 0x202     # RFLAGS
    push 0x1B      # CS (User Code)
    push rdi       # RIP (User Entry)

    iretq

# void ring3_exit(uint64_t status);
# RDI = status
.global ring3_exit
ring3_exit:
    mov rsp, [rip + kernel_ring3_rsp]
    mov qword ptr [rip + kernel_ring3_rsp], 0

    mov rax, rdi

    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx

    ret
