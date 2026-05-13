.intel_syntax noprefix
.code16
.include "boot_config.inc"
.equ E820_BUFFER, 0x5000
.equ E820_MAX_ENTRIES, 64
.equ BOOTINFO_VERSION, 1
.equ KERNEL_LOAD_BASE, 0x10000
.equ KERNEL_LOAD_LIMIT, 0x9f000
.equ KERNEL_LOAD_MAX_SECTORS, ((KERNEL_LOAD_LIMIT - KERNEL_LOAD_BASE) / 512)
.equ STAGE2_STACK_TOP, 0x7800
.if (STAGE2_SECTORS < 1) || (STAGE2_SECTORS > 64)
.error "STAGE2_SECTORS must fit the stage2 load window"
.endif
.if (KERNEL_SECTORS < 1) || (KERNEL_SECTORS > KERNEL_LOAD_MAX_SECTORS)
.error "KERNEL_SECTORS exceeds the fixed low-memory kernel load window"
.endif
.if (KERNEL_LBA < 1)
.error "KERNEL_LBA must be non-zero"
.endif
.section .text
.global stage2_start
stage2_start:
    cli
    cld
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7a00
    sti

    mov [boot_drive], dl
    call serial_init16
    mov si, offset msg_stage2
    call puts16
    call collect_e820
    call load_kernel
    call enable_a20
    mov si, offset msg_pm
    call puts16
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    .byte 0x66, 0xea
    .long protected_entry
    .word 0x18

puts16:
    lodsb
    test al, al
    jz puts_done
    call putc16
    jmp puts16
puts_done:
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

load_kernel:
    mov si, offset msg_load_kernel
    call puts16
    xor bx, bx
    mov word ptr [sectors_done], bx
.load_loop:
    mov ax, [sectors_done]
    cmp ax, KERNEL_SECTORS
    jae load_done
    mov cx, KERNEL_SECTORS
    sub cx, ax
    cmp cx, 32
    jbe .count_ready
    mov cx, 32
.count_ready:
    mov [dap_count], cx

    mov ax, [sectors_done]
    shl ax, 5
    add ax, KERNEL_LOAD_BASE >> 4
    mov [dap_segment], ax
    mov word ptr [dap_offset], 0

    xor ecx, ecx
    mov cx, [sectors_done]
    mov eax, (KERNEL_LBA & 0xffffffff)
    mov edx, (KERNEL_LBA >> 32)
    add eax, ecx
    adc edx, 0
    mov [dap_lba_low], eax
    mov [dap_lba_high], edx

    mov dl, [boot_drive]
    mov si, offset dap
    mov ah, 0x42
    int 0x13
    jc disk_error

    mov ax, [dap_count]
    add [sectors_done], ax
    jmp .load_loop
load_done:
    mov si, offset msg_kernel_loaded
    call puts16
    ret

collect_e820:
    mov si, offset msg_e820
    call puts16
    xor ax, ax
    mov es, ax
    mov di, E820_BUFFER
    xor ebx, ebx
    xor bp, bp
.next:
    mov eax, 0xe820
    mov edx, 0x534d4150
    mov ecx, 24
    mov dword ptr es:[di + 20], 1
    int 0x15
    jc .finish
    cmp eax, 0x534d4150
    jne .finish
    cmp ecx, 20
    jb .finish
    add di, 24
    inc bp
    cmp bp, E820_MAX_ENTRIES
    jae .finish
    test ebx, ebx
    jne .next
.finish:
    mov word ptr [bootinfo + 8], bp
    ret

enable_a20:
    call check_a20
    test ax, ax
    jnz .ok
    in al, 0x92
    or al, 0x02
    and al, 0xfe
    out 0x92, al
    call check_a20
    test ax, ax
    jnz .ok
    call enable_a20_kbc
    call check_a20
    test ax, ax
    jz a20_error
.ok:
    ret

wait_kbc_input_clear:
    push cx
    mov cx, 0xffff
.wait_kbc_clear:
    in al, 0x64
    test al, 0x02
    jz .ready_kbc_clear
    loop .wait_kbc_clear
.ready_kbc_clear:
    pop cx
    ret

enable_a20_kbc:
    call wait_kbc_input_clear
    mov al, 0xd1
    out 0x64, al
    call wait_kbc_input_clear
    mov al, 0xdf
    out 0x60, al
    call wait_kbc_input_clear
    ret

check_a20:
    pushf
    push ds
    push es
    push si
    push di
    cli
    xor ax, ax
    mov ds, ax
    mov si, 0x0500
    mov ax, 0xffff
    mov es, ax
    mov di, 0x0510
    mov al, byte ptr [si]
    mov byte ptr [a20_saved_low], al
    mov al, byte ptr es:[di]
    mov byte ptr [a20_saved_high], al
    mov byte ptr [si], 0x00
    mov byte ptr es:[di], 0xff
    cmp byte ptr [si], 0xff
    mov ax, 0
    je .restore
    mov ax, 1
.restore:
    mov bl, byte ptr [a20_saved_high]
    mov byte ptr es:[di], bl
    mov bl, byte ptr [a20_saved_low]
    mov byte ptr [si], bl
    pop di
    pop si
    pop es
    pop ds
    popf
    ret

disk_error:
    mov si, offset msg_disk_error
    call puts16
.hang:
    hlt
    jmp .hang

a20_error:
    mov si, offset msg_a20_error
    call puts16
.hang_a20:
    hlt
    jmp .hang_a20

.code32
check_cpu_features32:
    mov eax, 1
    cpuid
    test edx, (1 << 6)
    jz cpu_feature_error32
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb cpu_feature_error32
    mov eax, 0x80000001
    cpuid
    test edx, (1 << 20)
    jz cpu_feature_error32
    test edx, (1 << 29)
    jz cpu_feature_error32
    ret
cpu_feature_error32:
    cli
.hang_cpu_feature:
    hlt
    jmp .hang_cpu_feature

protected_entry:
    call check_cpu_features32
    mov ax, 0x20
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, STAGE2_STACK_TOP

    mov eax, offset pml4
    mov cr3, eax
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    mov ecx, 0xc0000080
    rdmsr
    // Enable long mode and NX page-table bit support.
    // Without EFER.NXE, every PTE with bit 63 set is reserved and user stacks
    // mapped as NX fault on the first push/call in ring3.
    or eax, (1 << 8) | (1 << 11)
    wrmsr

    mov eax, cr0
    or eax, 0x80000000
    mov cr0, eax
    .byte 0xea
    .long long_mode_entry
    .word 0x08

.code64
long_mode_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    mov rsp, STAGE2_STACK_TOP

    mov rsi, offset msg_lm_active
    call serial_puts64

    mov dword ptr [bootinfo + 0], 0x4155524f
    mov dword ptr [bootinfo + 4], BOOTINFO_VERSION
    xor rax, rax
    mov al, byte ptr [boot_drive]
    mov qword ptr [bootinfo + 16], rax
    mov qword ptr [bootinfo + 24], E820_BUFFER
    movabs rax, KERNEL_LBA
    mov qword ptr [bootinfo + 32], rax
    movabs rax, KERNEL_SECTORS
    mov qword ptr [bootinfo + 40], rax

    mov rsi, offset msg_jump_kernel
    call serial_puts64

    mov rdi, offset bootinfo
    mov rax, 0x10000
    jmp rax

serial_puts64:
    lodsb
    test al, al
    jz .puts_done64
    call serial_putc64
    jmp serial_puts64
.puts_done64:
    ret

serial_putc64:
    push rax
    push rcx
    push rdx
    mov ah, al
    mov ecx, 0x100000
.wait64:
    mov dx, 0x3fd
    in al, dx
    test al, 0x20
    jnz .ready64
    loop .wait64
.ready64:
    mov al, ah
    mov dx, 0x3f8
    out dx, al
    pop rdx
    pop rcx
    pop rax
    ret

.align 8
gdt:
    .quad 0x0000000000000000
    .quad 0x00af9a000000ffff
    .quad 0x00af92000000ffff
    .quad 0x00cf9a000000ffff
    .quad 0x00cf92000000ffff
gdt_descriptor:
    .word gdt_descriptor - gdt - 1
    .long gdt

.align 4096
pml4:
    .quad pdpt + 0x003
    .fill 511, 8, 0
.align 4096
pdpt:
    .quad pd + 0x003
    .fill 511, 8, 0
.align 4096
pd:
    .quad 0x0000000000000083
    .quad 0x0000000000200083
    .quad 0x0000000000400083
    .quad 0x0000000000600083
    .quad 0x0000000000800083
    .quad 0x0000000000a00083
    .quad 0x0000000000c00083
    .quad 0x0000000000e00083
    .fill 504, 8, 0

.code16
msg_stage2: .asciz "Aurora stage2: entered\r\n"
msg_e820: .asciz "Aurora stage2: collecting E820\r\n"
msg_load_kernel: .asciz "Aurora stage2: loading kernel\r\n"
msg_kernel_loaded: .asciz "Aurora stage2: kernel loaded\r\n"
msg_pm: .asciz "Aurora stage2: entering long mode\r\n"
msg_lm_active: .asciz "Aurora stage2: long mode active\r\n"
msg_jump_kernel: .asciz "Aurora stage2: jumping kernel\r\n"
msg_disk_error: .asciz "Aurora stage2: INT13 kernel read failed\r\n"
msg_a20_error: .asciz "Aurora stage2: A20 enable failed\r\n"
a20_saved_low: .byte 0
a20_saved_high: .byte 0
boot_drive: .byte 0
sectors_done: .word 0
.align 4
dap:
    .byte 0x10
    .byte 0
 dap_count: .word 0
 dap_offset: .word 0
 dap_segment: .word 0
 dap_lba_low: .long 0
 dap_lba_high: .long 0
.align 16
bootinfo:
    .space 64, 0
