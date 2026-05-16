.intel_syntax noprefix
.section .text.smp_trampoline, "ax"
.global smp_trampoline_start
.global smp_trampoline_end
.global smp_trampoline_cr3_offset
.global smp_trampoline_entry64_offset
.global smp_trampoline_stack_top_offset
.global smp_trampoline_logical_id_offset

.equ SMP_TRAMP_BASE, 0x7000
.equ CR0_PE, 0x00000001
.equ CR0_PG, 0x80000000
.equ CR0_WP, 0x00010000
.equ CR4_PAE, 0x00000020
.equ EFER_MSR, 0xc0000080
.equ EFER_LME, 0x00000100
.equ EFER_NXE, 0x00000800
.equ OFF_IDTR, smp_trampoline_idtr - smp_trampoline_start
.equ OFF_GDTR32, smp_trampoline_gdtr32 - smp_trampoline_start
.equ OFF_GDTR64, smp_trampoline_gdtr64 - smp_trampoline_start
.equ OFF_CR3, smp_trampoline_cr3 - smp_trampoline_start
.equ OFF_ENTRY64, smp_trampoline_entry64 - smp_trampoline_start
.equ OFF_STACK_TOP, smp_trampoline_stack_top - smp_trampoline_start
.equ OFF_LOGICAL_ID, smp_trampoline_logical_id - smp_trampoline_start
.equ OFF_PROT32, smp_trampoline_protected32 - smp_trampoline_start
.equ OFF_LONG64, smp_trampoline_long64 - smp_trampoline_start
.equ OFF_GDT, smp_trampoline_gdt - smp_trampoline_start

.code16
smp_trampoline_start:
    cli
    cld
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00
    lidt fword ptr [SMP_TRAMP_BASE + OFF_IDTR]
    lgdt fword ptr [SMP_TRAMP_BASE + OFF_GDTR32]
    mov eax, cr0
    or eax, CR0_PE
    mov cr0, eax
    .byte 0x66, 0xea
    .long SMP_TRAMP_BASE + OFF_PROT32
    .word 0x20

.code32
smp_trampoline_protected32:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, SMP_TRAMP_BASE + 0x1000
    and esp, 0xfffffff0

    mov eax, dword ptr [SMP_TRAMP_BASE + OFF_CR3]
    mov cr3, eax

    mov eax, cr4
    or eax, CR4_PAE
    mov cr4, eax

    mov ecx, EFER_MSR
    rdmsr
    or eax, (EFER_LME | EFER_NXE)
    wrmsr

    mov eax, cr0
    or eax, (CR0_PE | CR0_PG | CR0_WP)
    mov cr0, eax

    lgdt fword ptr [SMP_TRAMP_BASE + OFF_GDTR64]
    .byte 0xea
    .long SMP_TRAMP_BASE + OFF_LONG64
    .word 0x08

.code64
smp_trampoline_long64:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov rsp, qword ptr [SMP_TRAMP_BASE + OFF_STACK_TOP]
    and rsp, -16
    xor rbp, rbp
    mov edi, dword ptr [SMP_TRAMP_BASE + OFF_LOGICAL_ID]
    mov rax, qword ptr [SMP_TRAMP_BASE + OFF_ENTRY64]
    call rax
1:
    cli
    hlt
    jmp 1b

.align 8
smp_trampoline_idtr:
    .word 0
    .long 0

.align 8
smp_trampoline_gdtr32:
    .word smp_trampoline_gdt_end - smp_trampoline_gdt - 1
    .long SMP_TRAMP_BASE + OFF_GDT

.align 8
smp_trampoline_gdtr64:
    .word smp_trampoline_gdt_end - smp_trampoline_gdt - 1
    .quad SMP_TRAMP_BASE + OFF_GDT

.align 8
smp_trampoline_gdt:
    .quad 0x0000000000000000
    /* Mark descriptors as already accessed. After paging is enabled the
       trampoline page is executable/read-only; if the CPU had to set the
       accessed bit on the 64-bit code descriptor, it would fault before the
       kernel IDT/TSS are live and VMware reports that as a triple fault. */
    .quad 0x00af9b000000ffff
    .quad 0x00cf93000000ffff
    .quad 0x0000000000000000
    .quad 0x00cf9b000000ffff
smp_trampoline_gdt_end:

.align 8
smp_trampoline_cr3:
    .quad 0
smp_trampoline_entry64:
    .quad 0
smp_trampoline_stack_top:
    .quad 0
smp_trampoline_logical_id:
    .long 0
    .long 0

smp_trampoline_cr3_offset:
    .long OFF_CR3
smp_trampoline_entry64_offset:
    .long OFF_ENTRY64
smp_trampoline_stack_top_offset:
    .long OFF_STACK_TOP
smp_trampoline_logical_id_offset:
    .long OFF_LOGICAL_ID

.align 16
smp_trampoline_end:
