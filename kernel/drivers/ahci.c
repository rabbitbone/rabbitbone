#include <rabbitbone/ahci.h>
#include <rabbitbone/pci.h>
#include <rabbitbone/block.h>
#include <rabbitbone/memory.h>
#include <rabbitbone/vmm.h>
#include <rabbitbone/kmem.h>
#include <rabbitbone/libc.h>
#include <rabbitbone/log.h>
#include <rabbitbone/console.h>

#define MMIO_IDENTITY_ACCESS_LIMIT 0x0000800000000000ull

#define AHCI_MAX_CONTROLLERS 2u
#define AHCI_MAX_DEVS 4u
#define AHCI_MAX_WAIT 1000000u
#define AHCI_SECTOR_SIZE 512u

#define AHCI_GHC_AE (1u << 31)
#define AHCI_GHC_HR (1u << 0)
#define AHCI_PxCMD_ST  (1u << 0)
#define AHCI_PxCMD_FRE (1u << 4)
#define AHCI_PxCMD_FR  (1u << 14)
#define AHCI_PxCMD_CR  (1u << 15)
#define AHCI_PxIS_TFES (1u << 30)
#define AHCI_DEV_PRESENT 3u
#define AHCI_IPM_ACTIVE 1u
#define AHCI_SIG_ATA 0x00000101u

#define ATA_CMD_IDENTIFY_DEVICE 0xecu
#define ATA_CMD_READ_DMA_EXT    0x25u
#define ATA_CMD_WRITE_DMA_EXT   0x35u
#define ATA_CMD_FLUSH_CACHE_EXT 0xeau

typedef volatile struct ahci_port_regs {
    u32 clb;
    u32 clbu;
    u32 fb;
    u32 fbu;
    u32 is;
    u32 ie;
    u32 cmd;
    u32 rsv0;
    u32 tfd;
    u32 sig;
    u32 ssts;
    u32 sctl;
    u32 serr;
    u32 sact;
    u32 ci;
    u32 sntf;
    u32 fbs;
    u32 devslp;
    u32 rsv1[10];
    u32 vendor[4];
} ahci_port_regs_t;

typedef volatile struct ahci_hba_regs {
    u32 cap;
    u32 ghc;
    u32 is;
    u32 pi;
    u32 vs;
    u32 ccc_ctl;
    u32 ccc_pts;
    u32 em_loc;
    u32 em_ctl;
    u32 cap2;
    u32 bohc;
    u8 rsv[0xa0 - 0x2c];
    u8 vendor[0x100 - 0xa0];
    ahci_port_regs_t ports[32];
} ahci_hba_regs_t;

typedef struct RABBITBONE_PACKED ahci_prdt_entry {
    u32 dba;
    u32 dbau;
    u32 rsv0;
    u32 dbc_i;
} ahci_prdt_entry_t;

typedef struct RABBITBONE_PACKED ahci_cmd_table {
    u8 cfis[64];
    u8 acmd[16];
    u8 rsv[48];
    ahci_prdt_entry_t prdt[1];
} ahci_cmd_table_t;

typedef struct RABBITBONE_PACKED ahci_cmd_header {
    u16 flags;
    u16 prdtl;
    u32 prdbc;
    u32 ctba;
    u32 ctbau;
    u32 rsv[4];
} ahci_cmd_header_t;

typedef struct ahci_disk {
    block_device_t block;
    ahci_hba_regs_t *hba;
    ahci_port_regs_t *port;
    u8 pci_bus, pci_dev, pci_func;
    u8 port_no;
    u8 disk_no;
    u64 sectors;
    void *cmd_list;
    void *fis;
    ahci_cmd_table_t *cmd_table;
    void *bounce;
} ahci_disk_t;

static ahci_disk_t ahci_disks[AHCI_MAX_DEVS];
static usize ahci_disk_count;

static bool ahci_wait_clear(volatile u32 *reg, u32 mask) {
    for (u32 i = 0; i < AHCI_MAX_WAIT; ++i) if (((*reg) & mask) == 0) return true;
    return false;
}

static bool ahci_map_abar(u64 base, u64 size) {
    if (size == 0) size = 0x2000;
    if (base == 0 || base >= MMIO_IDENTITY_ACCESS_LIMIT) return false;
    u64 raw_end = 0;
    if (__builtin_add_overflow(base, size, &raw_end)) return false;
    if (raw_end > MMIO_IDENTITY_ACCESS_LIMIT) return false;
    u64 start = RABBITBONE_ALIGN_DOWN(base, PAGE_SIZE);
    u64 end = 0;
    if (!rabbitbone_align_up_u64_checked(raw_end, PAGE_SIZE, &end)) return false;
    if (end > MMIO_IDENTITY_ACCESS_LIMIT) return false;
    for (u64 p = start; p < end; p += PAGE_SIZE) {
        uptr phys = 0;
        u64 flags = 0;
        if (vmm_translate((uptr)p, &phys, &flags)) {
            if (phys != (uptr)p) return false;
            continue;
        }
        if (!vmm_map_4k((uptr)p, (uptr)p, VMM_WRITE | VMM_NX | VMM_NOCACHE | VMM_WRITETHR | VMM_GLOBAL)) return false;
    }
    return true;
}

static void ahci_free_port_allocations(ahci_disk_t *d) {
    if (!d) return;
    if (d->cmd_list) memory_free_page(d->cmd_list);
    if (d->fis) memory_free_page(d->fis);
    if (d->cmd_table) memory_free_page(d->cmd_table);
    if (d->bounce) memory_free_page(d->bounce);
    d->cmd_list = 0;
    d->fis = 0;
    d->cmd_table = 0;
    d->bounce = 0;
}

static bool ahci_stop_port(ahci_port_regs_t *p) {
    p->cmd &= ~AHCI_PxCMD_ST;
    if (!ahci_wait_clear(&p->cmd, AHCI_PxCMD_CR)) return false;
    p->cmd &= ~AHCI_PxCMD_FRE;
    if (!ahci_wait_clear(&p->cmd, AHCI_PxCMD_FR)) return false;
    return true;
}

static void ahci_start_port(ahci_port_regs_t *p) {
    p->cmd |= AHCI_PxCMD_FRE;
    p->cmd |= AHCI_PxCMD_ST;
}

static bool ahci_prepare_port(ahci_disk_t *d) {
    if (!ahci_stop_port(d->port)) return false;
    d->cmd_list = memory_alloc_page_below(MEMORY_KERNEL_DIRECT_LIMIT);
    d->fis = memory_alloc_page_below(MEMORY_KERNEL_DIRECT_LIMIT);
    d->cmd_table = (ahci_cmd_table_t *)memory_alloc_page_below(MEMORY_KERNEL_DIRECT_LIMIT);
    d->bounce = memory_alloc_page_below(MEMORY_KERNEL_DIRECT_LIMIT);
    if (!d->cmd_list || !d->fis || !d->cmd_table || !d->bounce) {
        ahci_free_port_allocations(d);
        return false;
    }
    memset(d->cmd_list, 0, PAGE_SIZE);
    memset(d->fis, 0, PAGE_SIZE);
    memset(d->cmd_table, 0, PAGE_SIZE);
    memset(d->bounce, 0, PAGE_SIZE);
    d->port->clb = (u32)((uptr)d->cmd_list & 0xffffffffu);
    d->port->clbu = (u32)((u64)(uptr)d->cmd_list >> 32);
    d->port->fb = (u32)((uptr)d->fis & 0xffffffffu);
    d->port->fbu = (u32)((u64)(uptr)d->fis >> 32);
    ahci_cmd_header_t *hdr = (ahci_cmd_header_t *)d->cmd_list;
    hdr[0].ctba = (u32)((uptr)d->cmd_table & 0xffffffffu);
    hdr[0].ctbau = (u32)((u64)(uptr)d->cmd_table >> 32);
    d->port->serr = 0xffffffffu;
    d->port->is = 0xffffffffu;
    ahci_start_port(d->port);
    return true;
}

static block_status_t ahci_issue_one(ahci_disk_t *d, u8 cmd, u64 lba, bool write, void *sector) {
    if (!d || !sector) return BLOCK_ERR_INVALID;
    if (!ahci_wait_clear(&d->port->tfd, 0x88u)) return BLOCK_ERR_TIMEOUT;
    ahci_cmd_header_t *hdr = (ahci_cmd_header_t *)d->cmd_list;
    memset(&hdr[0], 0, sizeof(hdr[0]));
    memset(d->cmd_table, 0, sizeof(*d->cmd_table));
    hdr[0].flags = 5u | (write ? (1u << 6) : 0u);
    hdr[0].prdtl = 1;
    hdr[0].ctba = (u32)((uptr)d->cmd_table & 0xffffffffu);
    hdr[0].ctbau = (u32)((u64)(uptr)d->cmd_table >> 32);
    d->cmd_table->prdt[0].dba = (u32)((uptr)sector & 0xffffffffu);
    d->cmd_table->prdt[0].dbau = (u32)((u64)(uptr)sector >> 32);
    d->cmd_table->prdt[0].dbc_i = (AHCI_SECTOR_SIZE - 1u) | (1u << 31);
    u8 *fis = d->cmd_table->cfis;
    fis[0] = 0x27; /* host-to-device FIS */
    fis[1] = 0x80;
    fis[2] = cmd;
    fis[4] = (u8)lba;
    fis[5] = (u8)(lba >> 8);
    fis[6] = (u8)(lba >> 16);
    fis[7] = 1u << 6;
    fis[8] = (u8)(lba >> 24);
    fis[9] = (u8)(lba >> 32);
    fis[10] = (u8)(lba >> 40);
    fis[12] = 1;
    fis[13] = 0;
    d->port->is = 0xffffffffu;
    d->port->ci = 1;
    for (u32 i = 0; i < AHCI_MAX_WAIT; ++i) {
        if ((d->port->ci & 1u) == 0) {
            if (d->port->is & AHCI_PxIS_TFES) return BLOCK_ERR_IO;
            return BLOCK_OK;
        }
        if (d->port->is & AHCI_PxIS_TFES) return BLOCK_ERR_IO;
    }
    return BLOCK_ERR_TIMEOUT;
}

static block_status_t ahci_issue_nodata(ahci_disk_t *d, u8 cmd) {
    if (!d) return BLOCK_ERR_INVALID;
    if (!ahci_wait_clear(&d->port->tfd, 0x88u)) return BLOCK_ERR_TIMEOUT;
    ahci_cmd_header_t *hdr = (ahci_cmd_header_t *)d->cmd_list;
    memset(&hdr[0], 0, sizeof(hdr[0]));
    memset(d->cmd_table, 0, sizeof(*d->cmd_table));
    hdr[0].flags = 5u;
    hdr[0].prdtl = 0;
    hdr[0].ctba = (u32)((uptr)d->cmd_table & 0xffffffffu);
    hdr[0].ctbau = (u32)((u64)(uptr)d->cmd_table >> 32);
    u8 *fis = d->cmd_table->cfis;
    fis[0] = 0x27;
    fis[1] = 0x80;
    fis[2] = cmd;
    fis[7] = 1u << 6;
    d->port->is = 0xffffffffu;
    d->port->ci = 1;
    for (u32 i = 0; i < AHCI_MAX_WAIT; ++i) {
        if ((d->port->ci & 1u) == 0) {
            if (d->port->is & AHCI_PxIS_TFES) return BLOCK_ERR_IO;
            return BLOCK_OK;
        }
        if (d->port->is & AHCI_PxIS_TFES) return BLOCK_ERR_IO;
    }
    return BLOCK_ERR_TIMEOUT;
}

static block_status_t ahci_read(block_device_t *bd, u64 lba, u32 count, void *buffer) {
    if (!bd || !bd->ctx || !buffer || count == 0) return BLOCK_ERR_INVALID;
    if (bd->sector_count && (lba >= bd->sector_count || (u64)count > bd->sector_count - lba)) return BLOCK_ERR_RANGE;
    usize total = 0;
    if (__builtin_mul_overflow((usize)count, (usize)AHCI_SECTOR_SIZE, &total)) return BLOCK_ERR_RANGE;
    (void)total;
    ahci_disk_t *d = (ahci_disk_t *)bd->ctx;
    u8 *out = (u8 *)buffer;
    for (u32 i = 0; i < count; ++i) {
        block_status_t st = ahci_issue_one(d, ATA_CMD_READ_DMA_EXT, lba + i, false, d->bounce);
        if (st != BLOCK_OK) return st;
        memcpy(out + (usize)i * AHCI_SECTOR_SIZE, d->bounce, AHCI_SECTOR_SIZE);
    }
    return BLOCK_OK;
}

static block_status_t ahci_write(block_device_t *bd, u64 lba, u32 count, const void *buffer) {
    if (!bd || !bd->ctx || !buffer || count == 0) return BLOCK_ERR_INVALID;
    if (bd->sector_count && (lba >= bd->sector_count || (u64)count > bd->sector_count - lba)) return BLOCK_ERR_RANGE;
    usize total = 0;
    if (__builtin_mul_overflow((usize)count, (usize)AHCI_SECTOR_SIZE, &total)) return BLOCK_ERR_RANGE;
    (void)total;
    ahci_disk_t *d = (ahci_disk_t *)bd->ctx;
    const u8 *in = (const u8 *)buffer;
    for (u32 i = 0; i < count; ++i) {
        memcpy(d->bounce, in + (usize)i * AHCI_SECTOR_SIZE, AHCI_SECTOR_SIZE);
        block_status_t st = ahci_issue_one(d, ATA_CMD_WRITE_DMA_EXT, lba + i, true, d->bounce);
        if (st != BLOCK_OK) return st;
    }
    block_status_t fl = ahci_issue_nodata(d, ATA_CMD_FLUSH_CACHE_EXT);
    return fl == BLOCK_OK ? BLOCK_OK : fl;
}

static block_status_t ahci_flush(block_device_t *bd) {
    if (!bd || !bd->ctx) return BLOCK_ERR_INVALID;
    ahci_disk_t *d = (ahci_disk_t *)bd->ctx;
    return ahci_issue_nodata(d, ATA_CMD_FLUSH_CACHE_EXT);
}

static u64 ahci_identify_sectors(const u16 *id) {
    u64 v = 0;
    v |= (u64)id[100];
    v |= (u64)id[101] << 16;
    v |= (u64)id[102] << 32;
    v |= (u64)id[103] << 48;
    if (v) return v;
    return (u64)id[60] | ((u64)id[61] << 16);
}

static bool ahci_probe_port(ahci_hba_regs_t *hba, const pci_device_t *pd, u8 port_no) {
    if (ahci_disk_count >= AHCI_MAX_DEVS) return false;
    ahci_port_regs_t *p = (ahci_port_regs_t *)&hba->ports[port_no];
    u32 ssts = p->ssts;
    if ((ssts & 0x0fu) != AHCI_DEV_PRESENT || ((ssts >> 8) & 0x0fu) != AHCI_IPM_ACTIVE) return false;
    if (p->sig != AHCI_SIG_ATA) return false;
    ahci_disk_t *d = &ahci_disks[ahci_disk_count];
    memset(d, 0, sizeof(*d));
    d->hba = hba;
    d->port = p;
    d->pci_bus = pd->bus;
    d->pci_dev = pd->device;
    d->pci_func = pd->function;
    d->port_no = port_no;
    d->disk_no = (u8)ahci_disk_count;
    if (!ahci_prepare_port(d)) {
        ahci_free_port_allocations(d);
        return false;
    }
    block_status_t st = ahci_issue_one(d, ATA_CMD_IDENTIFY_DEVICE, 0, false, d->bounce);
    if (st != BLOCK_OK) {
        KLOG(LOG_WARN, "ahci", "port %u identify failed: %s", port_no, block_status_name(st));
        ahci_free_port_allocations(d);
        return false;
    }
    d->sectors = ahci_identify_sectors((const u16 *)d->bounce);
    if (!d->sectors) { ahci_free_port_allocations(d); return false; }
    block_device_t *bd = &d->block;
    ksnprintf(bd->name, sizeof(bd->name), "ahci%u", d->disk_no);
    bd->driver = "ahci";
    bd->sector_count = d->sectors;
    bd->sector_size = AHCI_SECTOR_SIZE;
    bd->buffer_alignment = 1;
    bd->max_transfer_sectors = 128;
    bd->flags = BLOCKDEV_FLAG_WRITE | BLOCKDEV_FLAG_FLUSH | BLOCKDEV_FLAG_DMA;
    bd->ctx = d;
    bd->read = ahci_read;
    bd->write = ahci_write;
    bd->flush = ahci_flush;
    if (!block_register(bd)) { ahci_free_port_allocations(d); return false; }
    KLOG(LOG_INFO, "ahci", "registered %s pci=%02x:%02x.%u port=%u sectors=%llu", bd->name, pd->bus, pd->device, pd->function, port_no, (unsigned long long)d->sectors);
    ++ahci_disk_count;
    return true;
}

void ahci_init(void) {
    ahci_disk_count = 0;
    for (usize nth = 0; nth < AHCI_MAX_CONTROLLERS; ++nth) {
        const pci_device_t *pd = pci_find_class(0x01, 0x06, 0x01, nth);
        if (!pd) break;
        const pci_bar_info_t *abar = &pd->bars[5];
        if (!abar->base || (abar->flags & PCI_BAR_IO)) {
            KLOG(LOG_WARN, "ahci", "controller %02x:%02x.%u missing ABAR", pd->bus, pd->device, pd->function);
            continue;
        }
        if (!ahci_map_abar(abar->base, abar->size ? abar->size : 0x2000)) {
            KLOG(LOG_WARN, "ahci", "controller %02x:%02x.%u ABAR 0x%llx size=0x%llx is invalid or unmappable",
                 pd->bus, pd->device, pd->function, (unsigned long long)abar->base, (unsigned long long)abar->size);
            continue;
        }
        pci_config_write16(pd->bus, pd->device, pd->function, 0x04, pci_config_read16(pd->bus, pd->device, pd->function, 0x04) | 0x0006u);
        ahci_hba_regs_t *hba = (ahci_hba_regs_t *)(uptr)abar->base;
        hba->ghc |= AHCI_GHC_AE;
        u32 pi = hba->pi;
        KLOG(LOG_INFO, "ahci", "controller pci=%02x:%02x.%u abar=0x%llx pi=0x%x", pd->bus, pd->device, pd->function, (unsigned long long)abar->base, pi);
        for (u8 port = 0; port < 32u; ++port) {
            if (pi & (1u << port)) (void)ahci_probe_port(hba, pd, port);
        }
    }
    if (ahci_disk_count == 0) KLOG(LOG_INFO, "ahci", "no AHCI disks registered");
}
