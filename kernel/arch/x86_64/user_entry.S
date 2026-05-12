.intel_syntax noprefix
.section .data
.global arch_user_saved_rsp
.global arch_user_saved_rbp
.global arch_user_saved_rbx
.global arch_user_saved_r12
.global arch_user_saved_r13
.global arch_user_saved_r14
.global arch_user_saved_r15
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

# Private stack call bridge state. This bridge is intentionally single-owner:
# it is used only by privileged kernel control paths such as full ktest.
# The body must leave through arch_call_on_stack_finish(). Full ktest enters
# ring3 programs and exercises low-level return paths, so we must not depend on
# a return address stored on the temporary stack surviving until the end.
.align 8
arch_call_saved_rsp:
    .quad 0
arch_call_saved_rbp:
    .quad 0
arch_call_saved_rbx:
    .quad 0
arch_call_saved_r12:
    .quad 0
arch_call_saved_r13:
    .quad 0
arch_call_saved_r14:
    .quad 0
arch_call_saved_r15:
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
    mov rax, 0x202
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
    mov rax, [r10 + 152]
    or rax, 0x200
    and rax, 0x0000000000240fd7
    push rax
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


.global arch_call_on_stack
.type arch_call_on_stack, @function
# u64 arch_call_on_stack(u64 stack_top, void (*fn)(u64), u64 arg)
# Runs fn(arg) on a private kernel stack. The body is expected to call
# arch_call_on_stack_finish(result). If it accidentally returns, report failure.
arch_call_on_stack:
    # Arguments: rdi=stack_top, rsi=fn, rdx=arg.
    mov [rip + arch_call_saved_rsp], rsp
    mov [rip + arch_call_saved_rbp], rbp
    mov [rip + arch_call_saved_rbx], rbx
    mov [rip + arch_call_saved_r12], r12
    mov [rip + arch_call_saved_r13], r13
    mov [rip + arch_call_saved_r14], r14
    mov [rip + arch_call_saved_r15], r15

    mov r10, rsi
    mov r11, rdx

    mov rsp, rdi
    and rsp, -16
    sub rsp, 256
    and rsp, -16
    mov rdi, r11
    call r10

    # The body should be noreturn. If it comes back, restore the caller
    # context and return 0 rather than executing stale temporary-stack data.
    xor rdi, rdi
    jmp arch_call_on_stack_finish

.global arch_call_on_stack_finish
.type arch_call_on_stack_finish, @function
# void arch_call_on_stack_finish(u64 result) noreturn
arch_call_on_stack_finish:
    mov rax, rdi
    mov rsp, [rip + arch_call_saved_rsp]
    mov rbp, [rip + arch_call_saved_rbp]
    mov rbx, [rip + arch_call_saved_rbx]
    mov r12, [rip + arch_call_saved_r12]
    mov r13, [rip + arch_call_saved_r13]
    mov r14, [rip + arch_call_saved_r14]
    mov r15, [rip + arch_call_saved_r15]
    ret
