#include <aurora/arch/cpu.h>
#include <aurora/arch/io.h>
#include <aurora/console.h>

static inline u64 read_cr0(void) {
    u64 v;
    __asm__ volatile("mov %%cr0, %0" : "=r"(v));
    return v;
}

static inline void write_cr0(u64 v) {
    __asm__ volatile("mov %0, %%cr0" :: "r"(v) : "memory");
}

static inline u64 read_cr4(void) {
    u64 v;
    __asm__ volatile("mov %%cr4, %0" : "=r"(v));
    return v;
}

static inline void write_cr4(u64 v) {
    __asm__ volatile("mov %0, %%cr4" :: "r"(v) : "memory");
}

static inline void cpuid_leaf(u32 leaf, u32 subleaf, u32 *a, u32 *b, u32 *c, u32 *d) {
    u32 eax = leaf, ebx = 0, ecx = subleaf, edx = 0;
    __asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "+c"(ecx), "=d"(edx));
    if (a) *a = eax;
    if (b) *b = ebx;
    if (c) *c = ecx;
    if (d) *d = edx;
}

static bool cpu_has_leaf(u32 leaf) {
    u32 max_basic = 0;
    cpuid_leaf(0, 0, &max_basic, 0, 0, 0);
    return max_basic >= leaf;
}

void cpu_init(void) {
    __asm__ volatile("cld");

    u64 cr0 = read_cr0();
    cr0 &= ~(1ull << 2);
    cr0 |= (1ull << 1) | (1ull << 5) | (1ull << 16);
    write_cr0(cr0);

    u64 cr4 = read_cr4();
    cr4 |= (1ull << 9) | (1ull << 10);
    /* Do not enable SMEP in the early CPU bring-up path.  VMware and several
       BIOS boot configurations can expose CPUID.7.SMEP before the kernel has
       rebuilt and audited every active mapping after the bootloader handoff;
       a supervisor fetch from a page that still carries a user bit would fault
       before the IDT is installed and looks like an immediate virtual CPU
       shutdown.  NX/SSE are still enabled here; SMEP can be introduced later
       through a dedicated post-vmm page-table audit. */
    (void)cpu_has_leaf;
    write_cr4(cr4);
}

bool cpu_sse_enabled(void) {
    const u64 cr0 = read_cr0();
    const u64 cr4 = read_cr4();
    const bool em_clear = (cr0 & (1ull << 2)) == 0;
    const bool mp_set = (cr0 & (1ull << 1)) != 0;
    const bool osfxsr_set = (cr4 & (1ull << 9)) != 0;
    const bool osxmmexcpt_set = (cr4 & (1ull << 10)) != 0;
    return em_clear && mp_set && osfxsr_set && osxmmexcpt_set;
}

u64 cpu_read_efer(void) {
    u32 lo = 0, hi = 0;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0xc0000080u));
    return ((u64)hi << 32) | lo;
}

bool cpu_nxe_enabled(void) {
    return (cpu_read_efer() & (1ull << 11)) != 0;
}

const char *cpu_exception_name(u64 vector) {
    static const char *names[32] = {
        "divide error", "debug", "nmi", "breakpoint", "overflow", "bound range", "invalid opcode", "device not available",
        "double fault", "coprocessor segment", "invalid tss", "segment not present", "stack fault", "general protection", "page fault", "reserved",
        "x87 floating point", "alignment check", "machine check", "simd floating point", "virtualization", "control protection", "reserved", "reserved",
        "reserved", "reserved", "reserved", "reserved", "hypervisor injection", "vmm communication", "security", "reserved"
    };
    return vector < 32 ? names[vector] : "interrupt";
}

void cpu_dump_regs(const cpu_regs_t *r) {
    if (!r) return;
    kprintf("RIP=%p RSP=%p RFLAGS=%llx\n", (void *)(uptr)r->rip, (void *)(uptr)r->rsp, (unsigned long long)r->rflags);
    kprintf("RAX=%llx RBX=%llx RCX=%llx RDX=%llx\n", (unsigned long long)r->rax, (unsigned long long)r->rbx, (unsigned long long)r->rcx, (unsigned long long)r->rdx);
    kprintf("RSI=%llx RDI=%llx RBP=%llx\n", (unsigned long long)r->rsi, (unsigned long long)r->rdi, (unsigned long long)r->rbp);
}

void cpu_reboot(void) {
    cpu_cli();
    for (int i = 0; i < 1000; ++i) {
        u8 good = inb(0x64) & 0x02;
        if (!good) break;
    }
    outb(0x64, 0xfe);
    cpu_halt_forever();
}

AURORA_NORETURN void cpu_halt_forever(void) {
    cpu_cli();
    for (;;) cpu_hlt();
}
