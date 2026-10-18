bits 64

extern syscall_dispatch
extern syscall_user_rsp_ptr
extern syscall_kstack_top

section .bss
align 8
tmp_rax:
    resq 1

section .text
global syscall_entry
syscall_entry:
    cli

    mov [rel tmp_rax], rax
    mov rax, [rel syscall_user_rsp_ptr]
    mov [rax], rsp
    mov rsp, [rel syscall_kstack_top]
    mov rax, [rel tmp_rax]

    push r15
    push r14
    push r13
    push r12
    push rbp
    push rbx
    push rcx
    push r11

    push r9
    mov r9, r8
    mov r8, r10
    mov rcx, rdx
    mov rdx, rsi
    mov rsi, rdi
    mov rdi, rax
    call syscall_dispatch
    add rsp, 8

    pop r11
    pop rcx
    pop rbx
    pop rbp
    pop r12
    pop r13
    pop r14
    pop r15

    mov r10, rax
    mov rax, [rel syscall_user_rsp_ptr]
    mov rsp, [rax]
    mov rax, r10

    o64 sysret
