.intel_syntax noprefix
.section .text.boot
.global kernel_start
.extern kernel_main
.extern __bss_start
.extern __bss_end
kernel_start:
    cli
    mov rsp, 0x1f0000
    xor rbp, rbp
    mov r12, rdi

    lea rsi, [rip + msg_kernel_entry]
    call early_serial_write

    lea rdi, [rip + __bss_start]
    lea rcx, [rip + __bss_end]
    sub rcx, rdi
    xor eax, eax
    rep stosb

    lea rsi, [rip + msg_kernel_call_main]
    call early_serial_write

    mov rdi, r12
    call kernel_main
.halt:
    hlt
    jmp .halt

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

msg_kernel_entry:
    .asciz "Aurora kernel: entry reached\r\n"
msg_kernel_call_main:
    .asciz "Aurora kernel: calling kernel_main\r\n"
