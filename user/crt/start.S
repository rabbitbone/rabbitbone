.intel_syntax noprefix
.section .text
.global _start
.extern main
_start:
    xor rbp, rbp
    call main
    mov rdi, rax
    mov rax, 7
    int 0x80
.hang:
    mov rdi, rax
    mov rax, 7
    int 0x80
    pause
    jmp .hang
