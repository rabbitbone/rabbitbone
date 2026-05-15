#include <rabbitbone/acpi.h>
#include <rabbitbone/libc.h>
#include <rabbitbone/log.h>
#include <rabbitbone/format.h>
#include <rabbitbone/console.h>
#include <rabbitbone/types.h>
#include <rabbitbone/memory.h>

#define ACPI_RSDP_SIG "RSD PTR "
#define ACPI_TABLE_MADT "APIC"
#define ACPI_TABLE_HPET "HPET"

typedef struct RABBITBONE_PACKED acpi_rsdp_v1 {
    char signature[8];
    u8 checksum;
    char oem_id[6];
    u8 revision;
    u32 rsdt_address;
} acpi_rsdp_v1_t;

typedef struct RABBITBONE_PACKED acpi_rsdp_v2 {
    acpi_rsdp_v1_t v1;
    u32 length;
    u64 xsdt_address;
    u8 extended_checksum;
    u8 reserved[3];
} acpi_rsdp_v2_t;

typedef struct RABBITBONE_PACKED acpi_sdt_header {
    char signature[4];
    u32 length;
    u8 revision;
    u8 checksum;
    char oem_id[6];
    char oem_table_id[8];
    u32 oem_revision;
    u32 creator_id;
    u32 creator_revision;
} acpi_sdt_header_t;

typedef struct RABBITBONE_PACKED acpi_madt {
    acpi_sdt_header_t h;
    u32 lapic_address;
    u32 flags;
    u8 entries[];
} acpi_madt_t;

typedef struct RABBITBONE_PACKED acpi_madt_entry_header {
    u8 type;
    u8 length;
} acpi_madt_entry_header_t;

typedef struct RABBITBONE_PACKED acpi_madt_lapic {
    acpi_madt_entry_header_t h;
    u8 acpi_processor_id;
    u8 apic_id;
    u32 flags;
} acpi_madt_lapic_t;

typedef struct RABBITBONE_PACKED acpi_madt_ioapic {
    acpi_madt_entry_header_t h;
    u8 ioapic_id;
    u8 reserved;
    u32 ioapic_address;
    u32 gsi_base;
} acpi_madt_ioapic_t;

typedef struct RABBITBONE_PACKED acpi_madt_iso {
    acpi_madt_entry_header_t h;
    u8 bus;
    u8 source;
    u32 gsi;
    u16 flags;
} acpi_madt_iso_t;

typedef struct RABBITBONE_PACKED acpi_madt_lapic_override {
    acpi_madt_entry_header_t h;
    u16 reserved;
    u64 lapic_address;
} acpi_madt_lapic_override_t;

typedef struct RABBITBONE_PACKED acpi_gas {
    u8 address_space_id;
    u8 register_bit_width;
    u8 register_bit_offset;
    u8 access_size;
    u64 address;
} acpi_gas_t;

typedef struct RABBITBONE_PACKED acpi_hpet_table {
    acpi_sdt_header_t h;
    u32 event_timer_block_id;
    acpi_gas_t base_address;
    u8 hpet_number;
    u16 min_tick;
    u8 page_protection;
} acpi_hpet_table_t;

static acpi_info_t acpi_info;
static const acpi_sdt_header_t *acpi_tables[32];
static usize acpi_table_count;

static bool phys_range_identity_mapped(uptr addr, usize len) {
    if (len == 0) return true;
    uptr end = 0;
    if (__builtin_add_overflow(addr, (uptr)len, &end)) return false;
    return addr >= 0x400u && end <= (uptr)MEMORY_KERNEL_DIRECT_LIMIT;
}

static bool checksum_ok(const void *ptr, usize len) {
    if (!ptr || !phys_range_identity_mapped((uptr)ptr, len)) return false;
    const u8 *p = (const u8 *)ptr;
    u8 sum = 0;
    for (usize i = 0; i < len; ++i) sum = (u8)(sum + p[i]);
    return sum == 0;
}

static bool sig_eq(const char a[4], const char b[4]) {
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}

static bool acpi_span_has(const u8 *p, const u8 *end, usize len) {
    return p && end && p <= end && (usize)(end - p) >= len;
}

static const acpi_rsdp_v2_t *scan_rsdp_range(uptr start, uptr end) {
    uptr p = 0;
    u64 aligned = 0;
    if (start >= end || !rabbitbone_align_up_u64_checked((u64)start, 16u, &aligned) || (uptr)aligned != aligned) return 0;
    p = (uptr)aligned;
    for (;;) {
        uptr candidate_end = 0;
        if (__builtin_add_overflow(p, (uptr)sizeof(acpi_rsdp_v1_t), &candidate_end) || candidate_end > end) break;
        if (!phys_range_identity_mapped(p, sizeof(acpi_rsdp_v1_t))) goto next;
        const acpi_rsdp_v2_t *r = (const acpi_rsdp_v2_t *)p;
        if (memcmp(r->v1.signature, ACPI_RSDP_SIG, 8u) != 0) goto next;
        if (!checksum_ok(&r->v1, sizeof(acpi_rsdp_v1_t))) goto next;
        if (r->v1.revision >= 2u) {
            if (r->length < sizeof(acpi_rsdp_v2_t) || r->length > 4096u) goto next;
            if (!phys_range_identity_mapped((uptr)r, r->length) || !checksum_ok(r, r->length)) goto next;
        }
        return r;
next:
        if (__builtin_add_overflow(p, (uptr)16u, &p)) break;
    }
    return 0;
}

static const acpi_rsdp_v2_t *find_rsdp(void) {
    u16 ebda_seg = *(const volatile u16 *)(uptr)0x40e;
    uptr ebda = (uptr)ebda_seg << 4;
    if (ebda >= 0x80000u && ebda < 0xa0000u) {
        const acpi_rsdp_v2_t *r = scan_rsdp_range(ebda, ebda + 1024u);
        if (r) return r;
    }
    return scan_rsdp_range(0xe0000u, 0x100000u);
}

static bool valid_sdt(const acpi_sdt_header_t *h) {
    if (!h || !phys_range_identity_mapped((uptr)h, sizeof(*h))) return false;
    if (h->length < sizeof(*h) || h->length > (1024u * 1024u)) return false;
    if (!phys_range_identity_mapped((uptr)h, h->length)) return false;
    return checksum_ok(h, h->length);
}

static void add_table(const acpi_sdt_header_t *h) {
    if (!valid_sdt(h) || acpi_table_count >= RABBITBONE_ARRAY_LEN(acpi_tables)) return;
    acpi_tables[acpi_table_count++] = h;
}

static void parse_root_table(const acpi_sdt_header_t *root, bool xsdt) {
    if (!valid_sdt(root)) return;
    acpi_info.root_table = (u64)(uptr)root;
    acpi_info.xsdt = xsdt;
    usize entries = 0;
    if (xsdt) {
        if (root->length < sizeof(*root)) return;
        entries = (root->length - sizeof(*root)) / sizeof(u64);
        const u64 *p = (const u64 *)((const u8 *)root + sizeof(*root));
        for (usize i = 0; i < entries; ++i) {
            if (p[i] && p[i] < MEMORY_KERNEL_DIRECT_LIMIT) add_table((const acpi_sdt_header_t *)(uptr)p[i]);
        }
    } else {
        entries = (root->length - sizeof(*root)) / sizeof(u32);
        const u32 *p = (const u32 *)((const u8 *)root + sizeof(*root));
        for (usize i = 0; i < entries; ++i) {
            if (p[i]) add_table((const acpi_sdt_header_t *)(uptr)(u64)p[i]);
        }
    }
    acpi_info.table_count = (u32)acpi_table_count;
}

const void *acpi_find_table(const char sig[4]) {
    for (usize i = 0; i < acpi_table_count; ++i) {
        const acpi_sdt_header_t *h = acpi_tables[i];
        if (sig_eq(h->signature, sig)) return h;
    }
    return 0;
}

static void parse_madt(void) {
    const acpi_madt_t *m = (const acpi_madt_t *)acpi_find_table(ACPI_TABLE_MADT);
    if (!m || m->h.length < sizeof(*m)) return;
    acpi_info.madt_found = true;
    acpi_info.lapic_address = m->lapic_address;
    const u8 *p = m->entries;
    const u8 *end = (const u8 *)m + m->h.length;
    while (acpi_span_has(p, end, sizeof(acpi_madt_entry_header_t))) {
        const acpi_madt_entry_header_t *eh = (const acpi_madt_entry_header_t *)p;
        if (eh->length < sizeof(*eh) || !acpi_span_has(p, end, eh->length)) break;
        if (eh->type == 0 && eh->length >= sizeof(acpi_madt_lapic_t)) {
            const acpi_madt_lapic_t *e = (const acpi_madt_lapic_t *)p;
            if (acpi_info.cpu_count < ACPI_MAX_CPUS) {
                acpi_cpu_info_t *c = &acpi_info.cpus[acpi_info.cpu_count++];
                c->acpi_id = e->acpi_processor_id;
                c->apic_id = e->apic_id;
                c->flags = e->flags;
                c->enabled = (e->flags & 0x1u) != 0;
                if (c->enabled) ++acpi_info.enabled_cpu_count;
            }
        } else if (eh->type == 1 && eh->length >= sizeof(acpi_madt_ioapic_t)) {
            const acpi_madt_ioapic_t *e = (const acpi_madt_ioapic_t *)p;
            if (acpi_info.ioapic_count < ACPI_MAX_IOAPICS) {
                acpi_ioapic_info_t *io = &acpi_info.ioapics[acpi_info.ioapic_count++];
                io->id = e->ioapic_id;
                io->address = e->ioapic_address;
                io->gsi_base = e->gsi_base;
            }
        } else if (eh->type == 2 && eh->length >= sizeof(acpi_madt_iso_t)) {
            const acpi_madt_iso_t *e = (const acpi_madt_iso_t *)p;
            if (acpi_info.iso_count < ACPI_MAX_ISO) {
                acpi_iso_info_t *iso = &acpi_info.isos[acpi_info.iso_count++];
                iso->bus = e->bus;
                iso->source = e->source;
                iso->gsi = e->gsi;
                iso->flags = e->flags;
            }
        } else if (eh->type == 5 && eh->length >= sizeof(acpi_madt_lapic_override_t)) {
            const acpi_madt_lapic_override_t *e = (const acpi_madt_lapic_override_t *)p;
            if (e->lapic_address <= 0xffffffffull) acpi_info.lapic_address = (u32)e->lapic_address;
        }
        p += eh->length;
    }
}

static void parse_hpet(void) {
    const acpi_hpet_table_t *h = (const acpi_hpet_table_t *)acpi_find_table(ACPI_TABLE_HPET);
    if (!h || h->h.length < sizeof(*h)) return;
    if (h->base_address.address_space_id != 0u || h->base_address.address == 0) return;
    acpi_info.hpet_found = true;
    acpi_info.hpet_address = h->base_address.address;
    acpi_info.hpet_min_tick = h->min_tick;
}

void acpi_init(void) {
    memset(&acpi_info, 0, sizeof(acpi_info));
    memset(acpi_tables, 0, sizeof(acpi_tables));
    acpi_table_count = 0;
    const acpi_rsdp_v2_t *rsdp = find_rsdp();
    if (!rsdp) {
        KLOG(LOG_WARN, "acpi", "RSDP not found; ACPI/APIC/HPET discovery disabled");
        return;
    }
    acpi_info.found = true;
    acpi_info.rsdp = (u64)(uptr)rsdp;
    const acpi_sdt_header_t *root = 0;
    bool xsdt = false;
    if (rsdp->v1.revision >= 2u && rsdp->xsdt_address) {
        root = rsdp->xsdt_address < MEMORY_KERNEL_DIRECT_LIMIT ? (const acpi_sdt_header_t *)(uptr)rsdp->xsdt_address : 0;
        xsdt = root != 0;
        if (!valid_sdt(root)) { root = 0; xsdt = false; }
    }
    if (!root && rsdp->v1.rsdt_address) root = (const acpi_sdt_header_t *)(uptr)(u64)rsdp->v1.rsdt_address;
    parse_root_table(root, xsdt);
    parse_madt();
    parse_hpet();
    KLOG(LOG_INFO, "acpi", "rsdp=%p root=%p xsdt=%u tables=%u cpus=%u ioapics=%u hpet=%u",
         (void *)(uptr)acpi_info.rsdp, (void *)(uptr)acpi_info.root_table, acpi_info.xsdt ? 1u : 0u,
         acpi_info.table_count, acpi_info.enabled_cpu_count, acpi_info.ioapic_count, acpi_info.hpet_found ? 1u : 0u);
}

bool acpi_available(void) { return acpi_info.found; }
const acpi_info_t *acpi_get_info(void) { return &acpi_info; }


void acpi_format_status(char *out, usize out_len) {
    if (!out || out_len == 0) return;
    rabbitbone_buf_out_t bo;
    rabbitbone_buf_init(&bo, out, out_len);
    rabbitbone_buf_appendf(&bo, "acpi: found=%u rsdp=%p root=%p xsdt=%u tables=%u\n",
            acpi_info.found ? 1u : 0u, (void *)(uptr)acpi_info.rsdp, (void *)(uptr)acpi_info.root_table,
            acpi_info.xsdt ? 1u : 0u, acpi_info.table_count);
    rabbitbone_buf_appendf(&bo, "  madt=%u lapic=%p cpus=%u enabled=%u ioapics=%u iso=%u\n",
            acpi_info.madt_found ? 1u : 0u, (void *)(uptr)(u64)acpi_info.lapic_address,
            acpi_info.cpu_count, acpi_info.enabled_cpu_count, acpi_info.ioapic_count, acpi_info.iso_count);
    for (u32 i = 0; i < acpi_info.cpu_count; ++i) {
        const acpi_cpu_info_t *c = &acpi_info.cpus[i];
        rabbitbone_buf_appendf(&bo, "    cpu%u acpi=%u apic=%u enabled=%u flags=0x%x\n", i, c->acpi_id, c->apic_id, c->enabled ? 1u : 0u, c->flags);
    }
    for (u32 i = 0; i < acpi_info.ioapic_count; ++i) {
        const acpi_ioapic_info_t *io = &acpi_info.ioapics[i];
        rabbitbone_buf_appendf(&bo, "    ioapic%u id=%u addr=%p gsi_base=%u\n", i, io->id, (void *)(uptr)io->address, io->gsi_base);
    }
    rabbitbone_buf_appendf(&bo, "  hpet=%u base=%p min_tick=%u\n", acpi_info.hpet_found ? 1u : 0u,
            (void *)(uptr)acpi_info.hpet_address, acpi_info.hpet_min_tick);
}

bool acpi_selftest(void) {
    if (!acpi_info.found) return true;
    if (acpi_info.rsdp == 0 || acpi_info.root_table == 0) return false;
    if (acpi_info.table_count == 0) return false;
    if (acpi_info.madt_found && acpi_info.enabled_cpu_count == 0) return false;
    return true;
}
