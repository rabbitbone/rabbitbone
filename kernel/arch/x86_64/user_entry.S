.intel_syntax noprefix
.section .data
.global arch_user_saved_rsp
arch_user_saved_rsp:
    .quad 0
arch_user_saved_rbp:
    .quad 0
arch_user_saved_rbx:
    .quad 0
arch_user_saved_r12:
    .quad 0
arch_user_saved_r13:
    .quad 0
arch_user_saved_r14:
    .quad 0
arch_user_saved_r15:
    .quad 0

.section .text
.global arch_user_enter
.global arch_user_return_from_interrupt
.type arch_user_enter, @function
arch_user_enter:
    mov [rip + arch_user_saved_rsp], rsp
    mov [rip + arch_user_saved_rbp], rbp
    mov [rip + arch_user_saved_rbx], rbx
    mov [rip + arch_user_saved_r12], r12
    mov [rip + arch_user_saved_r13], r13
    mov [rip + arch_user_saved_r14], r14
    mov [rip + arch_user_saved_r15], r15

    cli
    mov ax, 0x1b
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov r10, rcx
    mov r11, r8
    push 0x1b
    push rsi
    pushfq
    pop rax
    or rax, 0x200
    push rax
    push 0x23
    push rdi
    mov rdi, rdx
    mov rsi, r10
    mov rdx, r11
    iretq
.arch_user_return_label:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    mov rbp, [rip + arch_user_saved_rbp]
    mov rbx, [rip + arch_user_saved_rbx]
    mov r12, [rip + arch_user_saved_r12]
    mov r13, [rip + arch_user_saved_r13]
    mov r14, [rip + arch_user_saved_r14]
    mov r15, [rip + arch_user_saved_r15]
    ret

.type arch_user_return_from_interrupt, @function
arch_user_return_from_interrupt:
    cli
    mov rsp, [rip + arch_user_saved_rsp]
    jmp .arch_user_return_label

.global arch_user_resume
.type arch_user_resume, @function
arch_user_resume:
    mov [rip + arch_user_saved_rsp], rsp
    mov [rip + arch_user_saved_rbp], rbp
    mov [rip + arch_user_saved_rbx], rbx
    mov [rip + arch_user_saved_r12], r12
    mov [rip + arch_user_saved_r13], r13
    mov [rip + arch_user_saved_r14], r14
    mov [rip + arch_user_saved_r15], r15

    mov r10, rdi
    cli
    mov ax, 0x1b
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push qword ptr [r10 + 168]
    push qword ptr [r10 + 160]
    push qword ptr [r10 + 152]
    push qword ptr [r10 + 144]
    push qword ptr [r10 + 136]

    mov r15, [r10 + 0]
    mov r14, [r10 + 8]
    mov r13, [r10 + 16]
    mov r12, [r10 + 24]
    mov r11, [r10 + 32]
    mov r9,  [r10 + 48]
    mov r8,  [r10 + 56]
    mov rsi, [r10 + 64]
    mov rdi, [r10 + 72]
    mov rbp, [r10 + 80]
    mov rdx, [r10 + 88]
    mov rcx, [r10 + 96]
    mov rbx, [r10 + 104]
    mov rax, [r10 + 112]
    mov r10, [r10 + 40]
    iretq
