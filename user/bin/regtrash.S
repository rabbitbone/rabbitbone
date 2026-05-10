.intel_syntax noprefix
.section .text
.global _start
_start:
    mov rbx, 0x1111222233334444
    mov rbp, 0x5555666677778888
    mov r12, 0x9999aaaabbbbcccc
    mov r13, 0xddddeeeeffff0001
    mov r14, 0x23456789abcdef01
    mov r15, 0xfedcba9876543210
    mov rdi, 23
    mov rax, 7
    int 0x80
.hang:
    pause
    jmp .hang
