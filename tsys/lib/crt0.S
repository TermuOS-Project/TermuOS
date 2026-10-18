    .section .text
    .global _start
    .extern main
    .extern exit

_start:
    xor %ebp, %ebp
    mov (%rsp), %rdi
    lea 8(%rsp), %rsi
    call main
    mov %rax, %rdi
    call exit
1:
    jmp 1b
