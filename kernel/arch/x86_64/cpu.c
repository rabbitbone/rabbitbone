#include <rabbitbone/arch/cpu.h>
#include <rabbitbone/arch/io.h>
#include <rabbitbone/console.h>
#include <rabbitbone/libc.h>
#include <rabbitbone/panic.h>

#define CPU_CR0_MP (1ull << 1)
#define CPU_CR0_EM (1ull << 2)
#define CPU_CR0_TS (1ull << 3)
#define CPU_CR0_NE (1ull << 5)
#define CPU_CR0_WP (1ull << 16)
#define CPU_CR4_OSFXSR (1ull << 9)
#define CPU_CR4_OSXMMEXCPT (1ull << 10)
#define CPU_CR4_OSXSAVE (1ull << 18)
#define CPU_MSR_EFER 0xc0000080u
#define CPU_MSR_PAT 0x277u
#define CPU_MSR_MTRR_CAP 0xfeu
#define CPU_MSR_MTRR_DEF_TYPE 0x2ffu
#define CPU_EFER_NXE (1ull << 11)
#define CPU_XCR0_X87 (1ull << 0)
#define CPU_XCR0_SSE (1ull << 1)

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

void cpu_cpuid(u32 leaf, u32 subleaf, u32 *a, u32 *b, u32 *c, u32 *d) {
    u32 eax = leaf, ebx = 0, ecx = subleaf, edx = 0;
    __asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "+c"(ecx), "=d"(edx));
    if (a) *a = eax;
    if (b) *b = ebx;
    if (c) *c = ecx;
    if (d) *d = edx;
}

static bool cpu_has_leaf(u32 leaf) {
    u32 max_basic = 0;
    cpu_cpuid(0, 0, &max_basic, 0, 0, 0);
    return max_basic >= leaf;
}

static bool cpu_has_extended_leaf(u32 leaf) {
    u32 max_ext = 0;
    cpu_cpuid(0x80000000u, 0, &max_ext, 0, 0, 0);
    return max_ext >= leaf;
}

static bool cpu_has_nx(void) {
    if (!cpu_has_extended_leaf(0x80000001u)) return false;
    u32 d = 0;
    cpu_cpuid(0x80000001u, 0, 0, 0, 0, &d);
    return (d & (1u << 20)) != 0;
}


static bool cpu_has_pat(void) {
    if (!cpu_has_leaf(1u)) return false;
    u32 d = 0;
    cpu_cpuid(1u, 0, 0, 0, 0, &d);
    return (d & (1u << 16)) != 0;
}

static void cpu_enable_pat_wc_entry(void) {
    if (!cpu_has_pat()) return;
    const u64 pat_wc = 0x01ull;
    u64 pat = cpu_read_msr(CPU_MSR_PAT);
    u64 old = pat;
    pat &= ~(0xffull << 8u);
    pat |= (pat_wc << 8u);
    if (pat != old) {
        __asm__ volatile("wbinvd" ::: "memory");
        cpu_write_msr(CPU_MSR_PAT, pat);
        __asm__ volatile("wbinvd" ::: "memory");
    }
}

static void cpu_enable_nxe(void) {
    if (!cpu_has_nx()) PANIC("cpu: NX bit is required but not supported");
    u64 efer = cpu_read_msr(CPU_MSR_EFER);
    if ((efer & CPU_EFER_NXE) == 0) cpu_write_msr(CPU_MSR_EFER, efer | CPU_EFER_NXE);
}

static bool cpu_has_leaf1_feature_ecx(u32 bit) {
    if (!cpu_has_leaf(1u)) return false;
    u32 c = 0;
    cpu_cpuid(1u, 0, 0, 0, &c, 0);
    return (c & (1u << bit)) != 0u;
}

static bool cpu_has_leaf1_feature_edx(u32 bit) {
    if (!cpu_has_leaf(1u)) return false;
    u32 d = 0;
    cpu_cpuid(1u, 0, 0, 0, 0, &d);
    return (d & (1u << bit)) != 0u;
}

static void cpu_init_fpu_unit(void) {
    __asm__ volatile("fninit" ::: "memory");
}

u64 cpu_read_xcr0(void) {
    if ((read_cr4() & CPU_CR4_OSXSAVE) == 0u) return 0;
    u32 lo = 0, hi = 0;
    __asm__ volatile("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0u));
    return ((u64)hi << 32) | lo;
}

static void cpu_write_xcr0(u64 value) {
    u32 lo = (u32)value;
    u32 hi = (u32)(value >> 32);
    __asm__ volatile("xsetbv" :: "c"(0u), "a"(lo), "d"(hi) : "memory");
}

bool cpu_xsave_enabled(void) {
    return (read_cr4() & CPU_CR4_OSXSAVE) != 0u && (cpu_read_xcr0() & (CPU_XCR0_X87 | CPU_XCR0_SSE)) == (CPU_XCR0_X87 | CPU_XCR0_SSE);
}

static u64 cpu_detect_feature_flags(void) {
    u64 flags = 0;
    if (cpu_has_leaf1_feature_edx(0u)) flags |= CPU_ARCH_FLAG_FPU;
    if (cpu_has_leaf1_feature_edx(24u)) flags |= CPU_ARCH_FLAG_FXSR;
    if (cpu_has_leaf1_feature_edx(25u)) flags |= CPU_ARCH_FLAG_SSE;
    if (cpu_has_leaf1_feature_edx(26u)) flags |= CPU_ARCH_FLAG_SSE2;
    if (cpu_has_leaf1_feature_ecx(26u)) flags |= CPU_ARCH_FLAG_XSAVE;
    if (cpu_has_leaf1_feature_ecx(27u)) flags |= CPU_ARCH_FLAG_OSXSAVE;
    if (cpu_has_leaf1_feature_ecx(28u)) flags |= CPU_ARCH_FLAG_AVX;
    if (cpu_has_nx()) flags |= CPU_ARCH_FLAG_NX;
    if (cpu_has_pat()) flags |= CPU_ARCH_FLAG_PAT;
    if (cpu_has_leaf1_feature_edx(12u)) flags |= CPU_ARCH_FLAG_MTRR;
    if (cpu_invariant_tsc_supported()) flags |= CPU_ARCH_FLAG_INVARIANT_TSC;
    if (cpu_has_leaf(7u)) {
        u32 b = 0;
        cpu_cpuid(7u, 0, 0, &b, 0, 0);
        if ((b & 1u) != 0u) flags |= CPU_ARCH_FLAG_FSGSBASE;
    }
    return flags;
}

static void cpu_decode_family_model(u32 eax, cpu_arch_state_t *out) {
    if (!out) return;
    u32 base_family = (eax >> 8u) & 0xfu;
    u32 base_model = (eax >> 4u) & 0xfu;
    u32 ext_family = (eax >> 20u) & 0xffu;
    u32 ext_model = (eax >> 16u) & 0xfu;
    out->stepping = eax & 0xfu;
    out->family = base_family == 0xfu ? base_family + ext_family : base_family;
    out->model = (base_family == 0x6u || base_family == 0xfu) ? (base_model | (ext_model << 4u)) : base_model;
}

void cpu_capture_arch_state(cpu_arch_state_t *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    cpu_cpuid(0u, 0, &out->max_basic_leaf, 0, 0, 0);
    cpu_cpuid(0x80000000u, 0, &out->max_extended_leaf, 0, 0, 0);
    if (cpu_has_leaf(1u)) {
        u32 a = 0;
        cpu_cpuid(1u, 0, &a, 0, 0, 0);
        cpu_decode_family_model(a, out);
    }
    out->features = cpu_detect_feature_flags();
    out->cr0 = read_cr0();
    out->cr4 = read_cr4();
    out->efer = cpu_read_msr(CPU_MSR_EFER);
    if ((out->features & CPU_ARCH_FLAG_PAT) != 0u) out->pat = cpu_read_msr(CPU_MSR_PAT);
    if ((out->features & CPU_ARCH_FLAG_MTRR) != 0u) {
        out->mtrr_cap = cpu_read_msr(CPU_MSR_MTRR_CAP);
        out->mtrr_default_type = cpu_read_msr(CPU_MSR_MTRR_DEF_TYPE);
    }
    if ((out->cr4 & CPU_CR4_OSXSAVE) != 0u) out->xcr0 = cpu_read_xcr0();
    if (cpu_has_leaf(0xdu)) {
        u32 a = 0, b = 0, c = 0, d = 0;
        cpu_cpuid(0xdu, 0u, &a, &b, &c, &d);
        out->xsave_xcr0_low = a;
        out->xsave_xcr0_high = d;
        out->xsave_area_size = b;
        out->xsave_area_max_size = c;
    }
    out->fpu_ready = ((out->features & CPU_ARCH_FLAG_FPU) != 0u && (out->cr0 & CPU_CR0_EM) == 0u && (out->cr0 & CPU_CR0_TS) == 0u) ? 1u : 0u;
    out->sse_ready = cpu_sse_enabled() ? 1u : 0u;
    out->xsave_ready = cpu_xsave_enabled() ? 1u : 0u;
    out->pat_wc_ready = ((out->features & CPU_ARCH_FLAG_PAT) != 0u && ((out->pat >> 8u) & 0xffu) == 0x01u) ? 1u : 0u;
    out->nxe_ready = ((out->efer & CPU_EFER_NXE) != 0u) ? 1u : 0u;
    out->invariant_tsc_ready = ((out->features & CPU_ARCH_FLAG_INVARIANT_TSC) != 0u) ? 1u : 0u;
    out->initialized = (out->fpu_ready != 0u && out->sse_ready != 0u && out->nxe_ready != 0u) ? 1u : 0u;
}

static void cpu_enable_xsave_if_supported(void) {
    if (!cpu_has_leaf1_feature_ecx(26u)) return;
    u64 cr4 = read_cr4();
    if ((cr4 & CPU_CR4_OSXSAVE) == 0u) {
        cr4 |= CPU_CR4_OSXSAVE;
        write_cr4(cr4);
    }
    u64 desired = CPU_XCR0_X87 | CPU_XCR0_SSE;
    u32 a = 0, d = 0;
    if (cpu_has_leaf(0xdu)) cpu_cpuid(0xdu, 0u, &a, 0, 0, &d);
    u64 supported = ((u64)d << 32u) | a;
    if ((supported & desired) == desired) {
        u64 xcr0 = cpu_read_xcr0();
        if ((xcr0 & desired) != desired) cpu_write_xcr0(xcr0 | desired);
    }
}

void cpu_init(void) {
    __asm__ volatile("cld");

    if (!cpu_has_leaf1_feature_edx(0u) || !cpu_has_leaf1_feature_edx(24u) || !cpu_has_leaf1_feature_edx(25u) || !cpu_has_leaf1_feature_edx(26u)) {
        PANIC("cpu: x86_64 FPU/FXSR/SSE/SSE2 baseline is required");
    }

    cpu_enable_nxe();
    cpu_enable_pat_wc_entry();

    u64 cr0 = read_cr0();
    cr0 &= ~(CPU_CR0_EM | CPU_CR0_TS);
    cr0 |= CPU_CR0_MP | CPU_CR0_NE | CPU_CR0_WP;
    write_cr0(cr0);
    cpu_init_fpu_unit();

    u64 cr4 = read_cr4();
    cr4 |= CPU_CR4_OSFXSR | CPU_CR4_OSXMMEXCPT;
    /* Do not enable SMEP in the early CPU bring-up path.  VMware and several
       BIOS boot configurations can expose CPUID.7.SMEP before the kernel has
       rebuilt and audited every active mapping after the bootloader handoff;
       a supervisor fetch from a page that still carries a user bit would fault
       before the IDT is installed and looks like an immediate virtual CPU
       shutdown.  NX/SSE are still enabled here; SMEP can be introduced later
       through a dedicated post-vmm page-table audit. */
    (void)cpu_has_leaf;
    write_cr4(cr4);
    cpu_enable_xsave_if_supported();
}

u64 cpu_read_msr(u32 msr) {
    u32 lo = 0, hi = 0;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((u64)hi << 32) | lo;
}

void cpu_write_msr(u32 msr, u64 value) {
    u32 lo = (u32)value;
    u32 hi = (u32)(value >> 32);
    __asm__ volatile("wrmsr" :: "c"(msr), "a"(lo), "d"(hi) : "memory");
}

u64 cpu_read_tsc(void) {
    u32 lo = 0, hi = 0;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((u64)hi << 32) | lo;
}

bool cpu_invariant_tsc_supported(void) {
    u32 max_ext = 0, a = 0, b = 0, c = 0, d = 0;
    cpu_cpuid(0x80000000u, 0, &max_ext, 0, 0, 0);
    if (max_ext < 0x80000007u) return false;
    cpu_cpuid(0x80000007u, 0, &a, &b, &c, &d);
    return (d & (1u << 8)) != 0;
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
    return cpu_read_msr(CPU_MSR_EFER);
}

bool cpu_nxe_enabled(void) {
    return (cpu_read_efer() & CPU_EFER_NXE) != 0;
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

RABBITBONE_NORETURN void cpu_halt_forever(void) {
    cpu_cli();
    for (;;) cpu_hlt();
}
