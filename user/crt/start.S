.intel_syntax noprefix
.section .text
.global _start
.extern main
# Loader enters with rdi=argc, rsi=argv and a 16-byte aligned user stack.
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
