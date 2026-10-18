BITS 64

global enter_userspace

section .text

enter_userspace:
    cli

    mov r8,  rdi         ; save entry
    mov r9,  rsi         ; save stack

    xor rax, rax
    xor rbx, rbx
    xor rcx, rcx
    xor rdx, rdx
    xor rsi, rsi
    xor rdi, rdi
    xor r10, r10
    xor r11, r11
    xor r12, r12
    xor r13, r13
    xor r14, r14
    xor r15, r15
    xor rbp, rbp

    mov ax, 0x1b
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push 0x1b            ; SS
    push r9              ; RSP
    push 0x202           ; RFLAGS
    push 0x23            ; CS
    push r8              ; RIP
    iretq
    