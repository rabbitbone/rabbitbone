.intel_syntax noprefix
.section .text.boot
.global kernel_start
.extern kernel_main
.extern __bss_start
.extern __bss_end
kernel_start:
    cli
    cld
    mov rsp, 0x1f0000
    xor rbp, rbp

    /*
     * The kernel ABI is SysV, so a correct loader passes bootinfo in RDI.
     * UEFI applications are built with the Win64 ABI, where the first argument
     * is RCX.  Keep a defensive fallback so a stale/buggy UEFI loader still
     * reaches kernel_main with the real bootinfo pointer instead of a random RDI.
     */
    mov r12, rdi
    mov rax, rdi
    call bootinfo_candidate_has_magic
    test al, al
    jnz .bootinfo_arg_ready
    mov rax, rcx
    call bootinfo_candidate_has_magic
    test al, al
    jz .bootinfo_arg_ready
    mov r12, rcx
.bootinfo_arg_ready:


    lea rdi, [rip + __bss_start]
    lea rcx, [rip + __bss_end]
    sub rcx, rdi
    xor eax, eax
    rep stosb


    mov rdi, r12
    call kernel_main
.halt:
    hlt
    jmp .halt

bootinfo_candidate_has_magic:
    test rax, rax
    jz .bootinfo_candidate_no
    cmp rax, 0x1000
    jb .bootinfo_candidate_no
    mov rdx, 0x40000000
    cmp rax, rdx
    jae .bootinfo_candidate_no
    cmp dword ptr [rax], 0x52424f4e
    jne .bootinfo_candidate_no
    cmp dword ptr [rax + 4], 2
    jne .bootinfo_candidate_no
    mov al, 1
    ret
.bootinfo_candidate_no:
    xor eax, eax
    ret

early_serial_write:
    lodsb
    test al, al
    jz .done
    call early_serial_putc
    jmp early_serial_write
.done:
    ret

early_serial_putc:
    push rax
    push rcx
    push rdx
    mov ah, al
    mov ecx, 0x100000
.wait:
    mov dx, 0x3fd
    in al, dx
    test al, 0x20
    jnz .ready
    loop .wait
.ready:
    mov al, ah
    mov dx, 0x3f8
    out dx, al
    pop rdx
    pop rcx
    pop rax
    ret
