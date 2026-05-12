.intel_syntax noprefix
.code16
.include "boot_config.inc"
.if (STAGE2_SECTORS < 1) || (STAGE2_SECTORS > 64)
.error "STAGE2_SECTORS must fit the 0x8000..0xffff stage2 load window"
.endif
.section .text
.global stage1_start
stage1_start:
    cli
    cld
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00
    sti

    mov [boot_drive], dl
    call serial_init16
    mov si, offset msg_stage1
    call puts16

    mov word ptr [dap_count], STAGE2_SECTORS
    mov word ptr [dap_offset], 0x8000
    mov word ptr [dap_segment], 0x0000
    mov dword ptr [dap_lba_low], 1
    mov dword ptr [dap_lba_high], 0
    mov si, offset dap
    mov dl, [boot_drive]
    mov ah, 0x42
    int 0x13
    jc disk_error

    mov si, offset msg_stage2_jump
    call puts16
    mov dl, [boot_drive]
    .byte 0xea
    .word 0x8000
    .word 0x0000

disk_error:
    mov si, offset msg_disk_error
    call puts16
.hang:
    hlt
    jmp .hang

puts16:
    lodsb
    test al, al
    jz .done
    call putc16
    jmp puts16
.done:
    ret

putc16:
    push ax
    push bx
    mov ah, 0x0e
    mov bx, 0x0007
    int 0x10
    pop bx
    pop ax
    call serial_putc16
    ret

serial_init16:
    push ax
    push dx
    mov dx, 0x3f9
    xor al, al
    out dx, al
    mov dx, 0x3fb
    mov al, 0x80
    out dx, al
    mov dx, 0x3f8
    mov al, 0x03
    out dx, al
    mov dx, 0x3f9
    xor al, al
    out dx, al
    mov dx, 0x3fb
    mov al, 0x03
    out dx, al
    mov dx, 0x3fa
    mov al, 0xc7
    out dx, al
    mov dx, 0x3fc
    mov al, 0x0b
    out dx, al
    pop dx
    pop ax
    ret

serial_putc16:
    push ax
    push cx
    push dx
    mov ah, al
    mov cx, 0xffff
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
    pop dx
    pop cx
    pop ax
    ret

msg_stage1: .asciz "Aurora stage1: BIOS boot sector entered\r\n"
msg_stage2_jump: .asciz "Aurora stage1: stage2 loaded, jumping\r\n"
msg_disk_error: .asciz "Aurora stage1: INT13 extended read failed\r\n"
boot_drive: .byte 0
.align 4
dap:
    .byte 0x10
    .byte 0
 dap_count: .word 0
 dap_offset: .word 0
 dap_segment: .word 0
 dap_lba_low: .long 0
 dap_lba_high: .long 0
