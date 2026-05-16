#include <rabbitbone/net.h>
#include <rabbitbone/pci.h>
#include <rabbitbone/memory.h>
#include <rabbitbone/vmm.h>
#include <rabbitbone/libc.h>
#include <rabbitbone/log.h>
#include <rabbitbone/format.h>
#include <rabbitbone/console.h>
#include <rabbitbone/drivers.h>
#include <rabbitbone/apic.h>
#include <rabbitbone/arch/idt.h>
#include <rabbitbone/arch/cpu.h>

#define ETH_MMIO_LIMIT 0x0000800000000000ull
#define ETH_DMA_LIMIT 0x100000000ull
#define ETH_MAX_ADAPTERS 1u
#define ETH_RX_RING 64u
#define ETH_TX_RING 64u
#define ETH_BUF_SIZE 2048u
#define ETH_BUF_PAGES 1u
#define ETH_WAIT 1000000u
#define ETH_MTA_REGS 128u
#define ETH_VFTA_REGS 128u

#define PCI_VENDOR_INTEL 0x8086u
#define PCI_COMMAND_IO 0x0001u
#define PCI_COMMAND_MEMORY 0x0002u
#define PCI_COMMAND_BUS_MASTER 0x0004u

#define REG_CTRL 0x00000u
#define REG_STATUS 0x00008u
#define REG_EERD 0x00014u
#define REG_CTRL_EXT 0x00018u
#define REG_ICR 0x000c0u
#define REG_ITR 0x000c4u
#define REG_ICS 0x000c8u
#define REG_IMS 0x000d0u
#define REG_IMC 0x000d8u
#define REG_RCTL 0x00100u
#define REG_TCTL 0x00400u
#define REG_TIPG 0x00410u
#define REG_RDBAL 0x02800u
#define REG_RDBAH 0x02804u
#define REG_RDLEN 0x02808u
#define REG_RDH 0x02810u
#define REG_RDT 0x02818u
#define REG_RDTR 0x02820u
#define REG_RXDCTL 0x02828u
#define REG_RADV 0x0282cu
#define REG_RSRPD 0x02c00u
#define REG_TDBAL 0x03800u
#define REG_TDBAH 0x03804u
#define REG_TDLEN 0x03808u
#define REG_TDH 0x03810u
#define REG_TDT 0x03818u
#define REG_TIDV 0x03820u
#define REG_TXDCTL 0x03828u
#define REG_TADV 0x0382cu
#define REG_RXCSUM 0x05000u
#define REG_MTA 0x05200u
#define REG_RAL0 0x05400u
#define REG_RAH0 0x05404u
#define REG_VFTA 0x05600u

#define CTRL_FD (1u << 0)
#define CTRL_LRST (1u << 3)
#define CTRL_ASDE (1u << 5)
#define CTRL_SLU (1u << 6)
#define CTRL_ILOS (1u << 7)
#define CTRL_RST (1u << 26)
#define CTRL_RFCE (1u << 27)
#define CTRL_TFCE (1u << 28)
#define CTRL_VME (1u << 30)
#define CTRL_PHY_RST (1u << 31)

#define STATUS_FD (1u << 0)
#define STATUS_LU (1u << 1)
#define STATUS_SPEED_100 (1u << 6)
#define STATUS_SPEED_1000 (1u << 7)

#define ICR_TXDW (1u << 0)
#define ICR_LSC (1u << 2)
#define ICR_RXSEQ (1u << 3)
#define ICR_RXDMT0 (1u << 4)
#define ICR_RXO (1u << 6)
#define ICR_RXT0 (1u << 7)
#define ICR_MDAC (1u << 9)
#define ICR_RXCFG (1u << 10)
#define ICR_PHYINT (1u << 12)
#define ICR_GPI (1u << 13)
#define ICR_ECCER (1u << 22)
#define IMS_DEFAULT (ICR_TXDW | ICR_LSC | ICR_RXSEQ | ICR_RXDMT0 | ICR_RXO | ICR_RXT0 | ICR_PHYINT | ICR_ECCER)

#define RCTL_EN (1u << 1)
#define RCTL_SBP (1u << 2)
#define RCTL_UPE (1u << 3)
#define RCTL_MPE (1u << 4)
#define RCTL_LPE (1u << 5)
#define RCTL_LBM_NONE (0u << 6)
#define RCTL_RDMTS_HALF (0u << 8)
#define RCTL_MO_0 (0u << 12)
#define RCTL_BAM (1u << 15)
#define RCTL_VFE (1u << 18)
#define RCTL_SECRC (1u << 26)

#define TCTL_EN (1u << 1)
#define TCTL_PSP (1u << 3)
#define TCTL_CT_SHIFT 4u
#define TCTL_COLD_SHIFT 12u
#define TCTL_RTLC (1u << 24)

#define RXCSUM_PCSD (1u << 13)
#define RXCSUM_IPOFL (1u << 8)
#define RXCSUM_TUOFL (1u << 9)

#define RXD_STAT_DD 0x01u
#define RXD_STAT_EOP 0x02u
#define RXD_STAT_IXSM 0x04u
#define RXD_STAT_VP 0x08u
#define RXD_ERR_IPE 0x40u
#define RXD_ERR_TCPE 0x20u
#define RXD_ERR_RXE 0x80u

#define TXD_CMD_EOP 0x01u
#define TXD_CMD_IFCS 0x02u
#define TXD_CMD_RS 0x08u
#define TXD_CMD_DEXT 0x20u
#define TXD_CMD_VLE 0x40u
#define TXD_STAT_DD 0x01u

#define RAH_AV (1u << 31)

#define DESC_CTL_ENABLE (1u << 25)

#define EERD_START_OLD (1u << 0)
#define EERD_DONE_OLD (1u << 4)
#define EERD_START_NEW (1u << 0)
#define EERD_DONE_NEW (1u << 1)

typedef struct RABBITBONE_PACKED eth_rx_desc {
    u64 addr;
    u16 length;
    u16 checksum;
    u8 status;
    u8 errors;
    u16 special;
} eth_rx_desc_t;

typedef struct RABBITBONE_PACKED eth_tx_desc {
    u64 addr;
    u16 length;
    u8 cso;
    u8 cmd;
    u8 status;
    u8 css;
    u16 special;
} eth_tx_desc_t;

typedef struct eth_adapter_info {
    u16 device_id;
    const char *name;
    bool vm_preferred;
} eth_adapter_info_t;

typedef struct eth_device {
    net_device_t net;
    const pci_device_t *pci;
    volatile u8 *mmio;
    u64 mmio_base;
    u64 mmio_size;
    eth_rx_desc_t *rx_desc;
    eth_tx_desc_t *tx_desc;
    void *rx_buffers[ETH_RX_RING];
    void *tx_buffers[ETH_TX_RING];
    u16 rx_next;
    u16 tx_next;
    u16 tx_clean;
    u16 tx_inflight;
    u8 irq_line;
    bool irq_installed;
    bool initialized;
    bool has_vlan_filter;
    bool rx_checksum;
    u32 rx_control;
    u32 tx_control;
    u32 interrupt_mask;
    u32 last_icr;
    u32 pci_id;
    u16 pci_command;
    const char *chip_name;
    spinlock_t hw_lock;
} eth_device_t;

static eth_device_t eth_devices[ETH_MAX_ADAPTERS];
static usize eth_device_count;
static bool eth_irq_vectors[16];
static bool eth_initialized;

static const eth_adapter_info_t eth_adapters[] = {
    { 0x100eu, "82540EM", true },
    { 0x100fu, "82545EM", true },
    { 0x1010u, "82546EB", true },
    { 0x1011u, "82545EM-C", true },
    { 0x1026u, "82545GM", true },
    { 0x1027u, "82545GM-C", true },
    { 0x10d3u, "82574L", true },
    { 0x10f6u, "82574LA", true },
    { 0x150cu, "82583V", true },
};
static inline void cpu_relax_eth(void) { __asm__ volatile("pause"); }
static inline void mmio_barrier(void) { __sync_synchronize(); }
static inline void eth_dma_wmb(void) { __sync_synchronize(); __asm__ volatile("sfence" ::: "memory"); }
static inline void eth_dma_rmb(void) { __asm__ volatile("lfence" ::: "memory"); __sync_synchronize(); }

static void eth_dma_flush_range(const void *ptr, usize len) {
    if (!ptr || len == 0) return;
    uptr start = (uptr)ptr & ~(uptr)63;
    uptr end = ((uptr)ptr + len + 63u) & ~(uptr)63;
    for (uptr p = start; p < end; p += 64u) {
        __asm__ volatile("clflush (%0)" :: "r"((const void *)p) : "memory");
    }
    __asm__ volatile("mfence" ::: "memory");
}

static inline u32 eth_rd(eth_device_t *d, u32 reg) {
    volatile u32 *p = (volatile u32 *)(d->mmio + reg);
    return *p;
}

static inline void eth_wr(eth_device_t *d, u32 reg, u32 value) {
    volatile u32 *p = (volatile u32 *)(d->mmio + reg);
    *p = value;
    mmio_barrier();
}

static inline void eth_flush(eth_device_t *d) { (void)eth_rd(d, REG_STATUS); }

static void eth_delay(u32 rounds) {
    for (volatile u32 i = 0; i < rounds; ++i) cpu_relax_eth();
}

static bool eth_map_mmio(u64 base, u64 size) {
    if (size == 0) size = 0x20000u;
    if (base == 0 || base >= ETH_MMIO_LIMIT) return false;
    u64 raw_end = 0;
    if (__builtin_add_overflow(base, size, &raw_end)) return false;
    if (raw_end > ETH_MMIO_LIMIT) return false;
    u64 start = RABBITBONE_ALIGN_DOWN(base, PAGE_SIZE);
    u64 end = 0;
    if (!rabbitbone_align_up_u64_checked(raw_end, PAGE_SIZE, &end)) return false;
    if (end > ETH_MMIO_LIMIT) return false;
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

static const eth_adapter_info_t *eth_lookup_adapter(u16 device_id) {
    for (usize i = 0; i < RABBITBONE_ARRAY_LEN(eth_adapters); ++i) {
        if (eth_adapters[i].device_id == device_id) return &eth_adapters[i];
    }
    return 0;
}

static bool eth_supported_pci(const pci_device_t *pd, const eth_adapter_info_t **info_out) {
    if (info_out) *info_out = 0;
    if (!pd || pd->vendor_id != PCI_VENDOR_INTEL) return false;
    const eth_adapter_info_t *info = eth_lookup_adapter(pd->device_id);
    if (!info) return false;
    if (info_out) *info_out = info;
    return true;
}
static bool eth_is_network_controller(const pci_device_t *pd) {
    return pd && pd->class_code == 0x02u;
}

static void eth_log_unsupported_network(const pci_device_t *pd) {
    if (!eth_is_network_controller(pd)) return;
    if (pd->vendor_id == 0x15adu && (pd->device_id == 0x07b0u || pd->device_id == 0x0720u)) {
        KLOG(LOG_WARN, "eth", "VMware network controller %04x:%04x is present; set virtualDev=e1000 or e1000e for this VM driver", pd->vendor_id, pd->device_id);
        return;
    }
    KLOG(LOG_INFO, "eth", "network controller not bound %02x:%02x.%u vendor=%04x device=%04x class=%02x:%02x:%02x",
         pd->bus, pd->device, pd->function, pd->vendor_id, pd->device_id, pd->class_code, pd->subclass, pd->prog_if);
}


static bool eth_mac_valid(const u8 mac[NET_ETH_ADDR_LEN]) {
    return mac && !net_mac_is_zero(mac) && !net_mac_is_multicast(mac);
}

static bool eth_read_eeprom_old(eth_device_t *d, u8 word, u16 *out) {
    if (!d || !out) return false;
    eth_wr(d, REG_EERD, EERD_START_OLD | ((u32)word << 8u));
    for (u32 i = 0; i < ETH_WAIT; ++i) {
        u32 v = eth_rd(d, REG_EERD);
        if (v & EERD_DONE_OLD) {
            *out = (u16)(v >> 16u);
            return true;
        }
    }
    return false;
}

static bool eth_read_eeprom_new(eth_device_t *d, u8 word, u16 *out) {
    if (!d || !out) return false;
    eth_wr(d, REG_EERD, EERD_START_NEW | ((u32)word << 2u));
    for (u32 i = 0; i < ETH_WAIT; ++i) {
        u32 v = eth_rd(d, REG_EERD);
        if (v & EERD_DONE_NEW) {
            *out = (u16)(v >> 16u);
            return true;
        }
    }
    return false;
}

static void eth_synthetic_mac(const pci_device_t *pd, u8 mac[NET_ETH_ADDR_LEN]) {
    mac[0] = 0x02u;
    mac[1] = 0x52u;
    mac[2] = 0x42u;
    mac[3] = pd ? pd->bus : 0u;
    mac[4] = pd ? (u8)((pd->device << 3u) | pd->function) : 0u;
    mac[5] = pd ? (u8)(pd->device_id ^ (pd->device_id >> 8u)) : 1u;
    if (!eth_mac_valid(mac)) mac[5] ^= 0x5au;
}

static void eth_read_mac(eth_device_t *d) {
    u32 ral = eth_rd(d, REG_RAL0);
    u32 rah = eth_rd(d, REG_RAH0);
    u8 mac[NET_ETH_ADDR_LEN];
    mac[0] = (u8)(ral & 0xffu);
    mac[1] = (u8)((ral >> 8u) & 0xffu);
    mac[2] = (u8)((ral >> 16u) & 0xffu);
    mac[3] = (u8)((ral >> 24u) & 0xffu);
    mac[4] = (u8)(rah & 0xffu);
    mac[5] = (u8)((rah >> 8u) & 0xffu);
    if ((rah & RAH_AV) && eth_mac_valid(mac)) {
        memcpy(d->net.mac, mac, sizeof(mac));
        return;
    }

    u16 w0 = 0, w1 = 0, w2 = 0;
    bool ok = eth_read_eeprom_new(d, 0, &w0) && eth_read_eeprom_new(d, 1, &w1) && eth_read_eeprom_new(d, 2, &w2);
    if (!ok) ok = eth_read_eeprom_old(d, 0, &w0) && eth_read_eeprom_old(d, 1, &w1) && eth_read_eeprom_old(d, 2, &w2);
    if (ok) {
        mac[0] = (u8)(w0 & 0xffu);
        mac[1] = (u8)(w0 >> 8u);
        mac[2] = (u8)(w1 & 0xffu);
        mac[3] = (u8)(w1 >> 8u);
        mac[4] = (u8)(w2 & 0xffu);
        mac[5] = (u8)(w2 >> 8u);
        if (eth_mac_valid(mac)) {
            memcpy(d->net.mac, mac, sizeof(mac));
            return;
        }
    }
    eth_synthetic_mac(d->pci, d->net.mac);
}

static void eth_write_mac(eth_device_t *d, const u8 mac[NET_ETH_ADDR_LEN]) {
    u32 ral = (u32)mac[0] | ((u32)mac[1] << 8u) | ((u32)mac[2] << 16u) | ((u32)mac[3] << 24u);
    u32 rah = (u32)mac[4] | ((u32)mac[5] << 8u) | RAH_AV;
    eth_wr(d, REG_RAL0, ral);
    eth_wr(d, REG_RAH0, rah);
    eth_flush(d);
}

static void eth_reset_dma_rings(eth_device_t *d) {
    if (!d || !d->rx_desc || !d->tx_desc) return;
    for (u32 i = 0; i < ETH_RX_RING; ++i) {
        d->rx_desc[i].addr = (u64)(uptr)d->rx_buffers[i];
        d->rx_desc[i].length = 0;
        d->rx_desc[i].checksum = 0;
        d->rx_desc[i].status = 0;
        d->rx_desc[i].errors = 0;
        d->rx_desc[i].special = 0;
    }
    for (u32 i = 0; i < ETH_TX_RING; ++i) {
        d->tx_desc[i].addr = (u64)(uptr)d->tx_buffers[i];
        d->tx_desc[i].length = 0;
        d->tx_desc[i].cso = 0;
        d->tx_desc[i].cmd = 0;
        d->tx_desc[i].status = TXD_STAT_DD;
        d->tx_desc[i].css = 0;
        d->tx_desc[i].special = 0;
    }
    d->rx_next = 0;
    d->tx_next = 0;
    d->tx_clean = 0;
    d->tx_inflight = 0;
    eth_dma_flush_range(d->rx_desc, ETH_RX_RING * sizeof(eth_rx_desc_t));
    eth_dma_flush_range(d->tx_desc, ETH_TX_RING * sizeof(eth_tx_desc_t));
}

static bool eth_alloc_rings(eth_device_t *d) {
    d->rx_desc = (eth_rx_desc_t *)memory_alloc_page_below(ETH_DMA_LIMIT);
    d->tx_desc = (eth_tx_desc_t *)memory_alloc_page_below(ETH_DMA_LIMIT);
    if (!d->rx_desc || !d->tx_desc) return false;
    memset(d->rx_desc, 0, PAGE_SIZE);
    memset(d->tx_desc, 0, PAGE_SIZE);
    for (u32 i = 0; i < ETH_RX_RING; ++i) {
        d->rx_buffers[i] = memory_alloc_contiguous_pages_below(ETH_BUF_PAGES, ETH_DMA_LIMIT);
        if (!d->rx_buffers[i]) return false;
        memset(d->rx_buffers[i], 0, ETH_BUF_SIZE);
        d->rx_desc[i].addr = (u64)(uptr)d->rx_buffers[i];
    }
    for (u32 i = 0; i < ETH_TX_RING; ++i) {
        d->tx_buffers[i] = memory_alloc_contiguous_pages_below(ETH_BUF_PAGES, ETH_DMA_LIMIT);
        if (!d->tx_buffers[i]) return false;
        memset(d->tx_buffers[i], 0, ETH_BUF_SIZE);
        d->tx_desc[i].addr = (u64)(uptr)d->tx_buffers[i];
        d->tx_desc[i].status = TXD_STAT_DD;
    }
    eth_reset_dma_rings(d);
    return true;
}

static void eth_free_rings(eth_device_t *d) {
    if (!d) return;
    for (u32 i = 0; i < ETH_RX_RING; ++i) {
        if (d->rx_buffers[i]) memory_free_contiguous_pages(d->rx_buffers[i], ETH_BUF_PAGES);
        d->rx_buffers[i] = 0;
    }
    for (u32 i = 0; i < ETH_TX_RING; ++i) {
        if (d->tx_buffers[i]) memory_free_contiguous_pages(d->tx_buffers[i], ETH_BUF_PAGES);
        d->tx_buffers[i] = 0;
    }
    if (d->rx_desc) memory_free_page(d->rx_desc);
    if (d->tx_desc) memory_free_page(d->tx_desc);
    d->rx_desc = 0;
    d->tx_desc = 0;
}

static void eth_disable_irq(eth_device_t *d) {
    eth_wr(d, REG_IMC, 0xffffffffu);
    eth_flush(d);
    (void)eth_rd(d, REG_ICR);
}

static void eth_enable_irq(eth_device_t *d) {
    d->interrupt_mask = IMS_DEFAULT;
    eth_wr(d, REG_IMS, d->interrupt_mask);
    eth_flush(d);
}

static void eth_set_rx_mode(eth_device_t *d) {
    u32 rctl = d->rx_control;
    if (d->net.flags & NETDEV_F_PROMISC) rctl |= RCTL_UPE | RCTL_MPE;
    else rctl &= ~RCTL_UPE;
    if (d->net.flags & NETDEV_F_ALLMULTI) rctl |= RCTL_MPE;
    if (d->has_vlan_filter) rctl |= RCTL_VFE;
    d->rx_control = rctl;
    eth_wr(d, REG_RCTL, rctl);
    eth_flush(d);
}

static void eth_clear_tables(eth_device_t *d) {
    for (u32 i = 0; i < ETH_MTA_REGS; ++i) eth_wr(d, REG_MTA + i * 4u, 0);
    for (u32 i = 0; i < ETH_VFTA_REGS; ++i) eth_wr(d, REG_VFTA + i * 4u, 0);
}

static void eth_update_link_locked(eth_device_t *d) {
    u32 st = eth_rd(d, REG_STATUS);
    bool up = (st & STATUS_LU) != 0;
    u32 speed = 10u;
    if (st & STATUS_SPEED_1000) speed = 1000u;
    else if (st & STATUS_SPEED_100) speed = 100u;
    u32 duplex = (st & STATUS_FD) ? NET_LINK_FULL : NET_LINK_HALF;
    netdev_set_link(&d->net, up, speed, duplex);
}

static bool eth_reset_hw(eth_device_t *d) {
    eth_disable_irq(d);
    eth_wr(d, REG_RCTL, 0);
    eth_wr(d, REG_TCTL, 0);
    eth_flush(d);
    eth_delay(50000u);
    u32 ctrl = eth_rd(d, REG_CTRL);
    ctrl |= CTRL_RST;
    eth_wr(d, REG_CTRL, ctrl);
    eth_flush(d);
    for (u32 i = 0; i < ETH_WAIT; ++i) {
        if ((eth_rd(d, REG_CTRL) & CTRL_RST) == 0) break;
        cpu_relax_eth();
    }
    eth_delay(200000u);
    eth_disable_irq(d);
    u32 after = eth_rd(d, REG_CTRL);
    after |= CTRL_SLU | CTRL_ASDE | CTRL_FD | CTRL_VME;
    after &= ~(CTRL_LRST | CTRL_PHY_RST | CTRL_ILOS);
    eth_wr(d, REG_CTRL, after);
    eth_flush(d);
    return true;
}

static bool eth_configure_hw(eth_device_t *d) {
    if (!d || !d->rx_desc || !d->tx_desc) return false;
    eth_reset_dma_rings(d);
    eth_read_mac(d);
    eth_write_mac(d, d->net.mac);
    eth_clear_tables(d);

    eth_wr(d, REG_RDBAL, (u32)((u64)(uptr)d->rx_desc & 0xffffffffu));
    eth_wr(d, REG_RDBAH, (u32)((u64)(uptr)d->rx_desc >> 32u));
    eth_wr(d, REG_RDLEN, ETH_RX_RING * sizeof(eth_rx_desc_t));
    eth_wr(d, REG_RDH, 0);
    eth_wr(d, REG_RDT, ETH_RX_RING - 1u);
    eth_wr(d, REG_RDTR, 0);
    eth_wr(d, REG_RADV, 0);
    eth_wr(d, REG_RSRPD, 0);
    eth_wr(d, REG_RXDCTL, DESC_CTL_ENABLE | (8u << 0u) | (8u << 8u) | (4u << 16u));
    eth_flush(d);
    d->rx_next = 0;

    eth_wr(d, REG_TDBAL, (u32)((u64)(uptr)d->tx_desc & 0xffffffffu));
    eth_wr(d, REG_TDBAH, (u32)((u64)(uptr)d->tx_desc >> 32u));
    eth_wr(d, REG_TDLEN, ETH_TX_RING * sizeof(eth_tx_desc_t));
    eth_wr(d, REG_TDH, 0);
    eth_wr(d, REG_TDT, 0);
    eth_wr(d, REG_TIDV, 0);
    eth_wr(d, REG_TADV, 0);
    eth_wr(d, REG_TXDCTL, DESC_CTL_ENABLE | (8u << 0u) | (8u << 8u) | (4u << 16u));
    eth_flush(d);
    d->tx_next = 0;
    d->tx_clean = 0;
    d->tx_inflight = 0;

    eth_wr(d, REG_TIPG, 0x0060200au);
    d->tx_control = TCTL_EN | TCTL_PSP | (0x10u << TCTL_CT_SHIFT) | (0x40u << TCTL_COLD_SHIFT) | TCTL_RTLC;
    eth_wr(d, REG_TCTL, d->tx_control);

    eth_wr(d, REG_RXCSUM, 0);
    d->rx_checksum = false;
    d->rx_control = RCTL_EN | RCTL_BAM | RCTL_SECRC | RCTL_LBM_NONE | RCTL_RDMTS_HALF | RCTL_MO_0;
    eth_set_rx_mode(d);
    eth_wr(d, REG_ITR, 256u);
    eth_enable_irq(d);
    eth_update_link_locked(d);
    return true;
}

static void eth_reclaim_tx(eth_device_t *d) {
    if (!d || !d->tx_desc) return;
    eth_dma_rmb();
    while (d->tx_inflight != 0) {
        volatile eth_tx_desc_t *tx = (volatile eth_tx_desc_t *)&d->tx_desc[d->tx_clean];
        if ((tx->status & TXD_STAT_DD) == 0) break;
        d->tx_clean = (u16)((d->tx_clean + 1u) % ETH_TX_RING);
        --d->tx_inflight;
    }
}

static void eth_process_rx(eth_device_t *d) {
    if (!d || !d->initialized) return;
    for (u32 budget = 0; budget < ETH_RX_RING; ++budget) {
        eth_rx_desc_t *rx = &d->rx_desc[d->rx_next];
        eth_dma_flush_range(rx, sizeof(*rx));
        eth_dma_rmb();
        if ((rx->status & RXD_STAT_DD) == 0) break;
        u16 len = rx->length;
        bool ok = (rx->status & RXD_STAT_EOP) && len >= NET_ETH_HEADER_LEN && len <= NET_ETH_MAX_FRAME && (rx->errors & RXD_ERR_RXE) == 0;
        if (ok) {
            const u8 *buf = (const u8 *)d->rx_buffers[d->rx_next];
            eth_dma_flush_range(buf, len);
            net_rx_meta_t meta;
            memset(&meta, 0, sizeof(meta));
            meta.flags = NET_RX_OK;
            if ((rx->status & RXD_STAT_IXSM) == 0 && d->rx_checksum) meta.flags |= NET_RX_CSUM_OK;
            if (rx->status & RXD_STAT_VP) {
                meta.flags |= NET_RX_VLAN;
                meta.vlan_tag = rx->special;
            }
            if (len >= NET_ETH_HEADER_LEN) meta.protocol = ((u16)buf[12] << 8u) | buf[13];
            (void)netdev_receive(&d->net, buf, len, &meta);
        } else {
            u64 flags = spin_lock_irqsave(&d->net.lock);
            ++d->net.stats.rx_errors;
            spin_unlock_irqrestore(&d->net.lock, flags);
        }
        rx->status = 0;
        rx->errors = 0;
        rx->length = 0;
        eth_dma_flush_range(rx, sizeof(*rx));
        eth_dma_wmb();
        eth_wr(d, REG_RDT, d->rx_next);
        d->rx_next = (u16)((d->rx_next + 1u) % ETH_RX_RING);
    }
}

static void eth_irq_common(cpu_regs_t *regs) {
    RABBITBONE_UNUSED(regs);
    for (usize i = 0; i < eth_device_count; ++i) {
        eth_device_t *d = &eth_devices[i];
        if (!d->initialized || !d->mmio) continue;
        u32 cause = eth_rd(d, REG_ICR);
        if (!cause) continue;
        d->last_icr = cause;
        u64 flags = spin_lock_irqsave(&d->net.lock);
        ++d->net.stats.interrupts;
        if (cause & (ICR_RXO | ICR_RXSEQ | ICR_ECCER)) ++d->net.stats.rx_errors;
        spin_unlock_irqrestore(&d->net.lock, flags);
        if (cause & (ICR_LSC | ICR_PHYINT | ICR_GPI)) eth_update_link_locked(d);
        if (cause & (ICR_RXT0 | ICR_RXDMT0 | ICR_RXO | ICR_RXSEQ)) eth_process_rx(d);
        if (cause & ICR_TXDW) eth_reclaim_tx(d);
    }
}

static void eth_install_irq(eth_device_t *d) {
    if (!d || d->irq_line >= 16u || d->irq_line == 0u || d->irq_line == 1u) return;
    u8 vector = (u8)(32u + d->irq_line);
    if (!eth_irq_vectors[d->irq_line]) {
        idt_set_handler(vector, eth_irq_common);
        pic_clear_mask(d->irq_line);
        eth_irq_vectors[d->irq_line] = true;
    }
    d->irq_installed = true;
    d->net.flags |= NETDEV_F_IRQ;
}

static net_status_t eth_open(net_device_t *ndev) {
    if (!ndev || !ndev->ctx) return NET_ERR_NODEV;
    eth_device_t *d = (eth_device_t *)ndev->ctx;
    u64 flags = spin_lock_irqsave(&d->hw_lock);
    bool ok = eth_reset_hw(d) && eth_configure_hw(d);
    if (ok) d->initialized = true;
    spin_unlock_irqrestore(&d->hw_lock, flags);
    if (!ok) return NET_ERR_IO;
    eth_install_irq(d);
    return NET_OK;
}

static void eth_close(net_device_t *ndev) {
    if (!ndev || !ndev->ctx) return;
    eth_device_t *d = (eth_device_t *)ndev->ctx;
    u64 flags = spin_lock_irqsave(&d->hw_lock);
    eth_disable_irq(d);
    eth_wr(d, REG_RCTL, 0);
    eth_wr(d, REG_TCTL, 0);
    d->initialized = false;
    spin_unlock_irqrestore(&d->hw_lock, flags);
}

static net_status_t eth_transmit(net_device_t *ndev, const void *frame, usize len, const net_tx_meta_t *meta) {
    if (!ndev || !ndev->ctx || !frame) return NET_ERR_INVAL;
    if (len > ETH_BUF_SIZE || len < NET_ETH_HEADER_LEN) return NET_ERR_RANGE;
    eth_device_t *d = (eth_device_t *)ndev->ctx;
    if (!d->initialized) return NET_ERR_NODEV;
    if (meta && (meta->flags & (NET_TX_CSUM | NET_TX_TSO))) return NET_ERR_UNSUPPORTED;

    u64 flags = spin_lock_irqsave(&d->hw_lock);
    eth_reclaim_tx(d);
    if (d->tx_inflight >= ETH_TX_RING - 1u) {
        spin_unlock_irqrestore(&d->hw_lock, flags);
        return NET_ERR_BUSY;
    }
    eth_tx_desc_t *tx = &d->tx_desc[d->tx_next];

    usize wire_len = len < NET_ETH_MIN_FRAME ? NET_ETH_MIN_FRAME : len;
    memset(d->tx_buffers[d->tx_next], 0, ETH_BUF_SIZE);
    memcpy(d->tx_buffers[d->tx_next], frame, len);
    eth_dma_flush_range(d->tx_buffers[d->tx_next], wire_len);

    tx->addr = (u64)(uptr)d->tx_buffers[d->tx_next];
    tx->length = (u16)wire_len;
    tx->cso = 0;
    tx->cmd = TXD_CMD_EOP | TXD_CMD_IFCS | TXD_CMD_RS;
    tx->css = 0;
    tx->special = 0;
    if (meta && (meta->flags & NET_TX_VLAN)) {
        tx->cmd |= TXD_CMD_VLE;
        tx->special = meta->vlan_tag;
    }
    tx->status = 0;
    eth_dma_flush_range(tx, sizeof(*tx));
    eth_dma_wmb();

    d->tx_next = (u16)((d->tx_next + 1u) % ETH_TX_RING);
    ++d->tx_inflight;
    eth_wr(d, REG_TDT, d->tx_next);
    eth_flush(d);
    spin_unlock_irqrestore(&d->hw_lock, flags);
    return NET_OK;
}

static void eth_poll_netdev(net_device_t *ndev) {
    if (!ndev || !ndev->ctx) return;
    eth_device_t *d = (eth_device_t *)ndev->ctx;
    if (!d->initialized) return;
    u32 cause = eth_rd(d, REG_ICR);
    if (cause) {
        d->last_icr = cause;
        if (cause & (ICR_LSC | ICR_PHYINT | ICR_GPI)) eth_update_link_locked(d);
    }
    eth_reclaim_tx(d);
    eth_process_rx(d);
}

static net_status_t eth_set_promisc(net_device_t *ndev, bool enabled) {
    if (!ndev || !ndev->ctx) return NET_ERR_NODEV;
    eth_device_t *d = (eth_device_t *)ndev->ctx;
    u64 flags = spin_lock_irqsave(&d->hw_lock);
    if (enabled) ndev->flags |= NETDEV_F_PROMISC;
    else ndev->flags &= ~NETDEV_F_PROMISC;
    eth_set_rx_mode(d);
    spin_unlock_irqrestore(&d->hw_lock, flags);
    return NET_OK;
}

static net_status_t eth_set_allmulti(net_device_t *ndev, bool enabled) {
    if (!ndev || !ndev->ctx) return NET_ERR_NODEV;
    eth_device_t *d = (eth_device_t *)ndev->ctx;
    u64 flags = spin_lock_irqsave(&d->hw_lock);
    if (enabled) ndev->flags |= NETDEV_F_ALLMULTI;
    else ndev->flags &= ~NETDEV_F_ALLMULTI;
    eth_set_rx_mode(d);
    spin_unlock_irqrestore(&d->hw_lock, flags);
    return NET_OK;
}

static net_status_t eth_set_mac_netdev(net_device_t *ndev, const u8 mac[NET_ETH_ADDR_LEN]) {
    if (!ndev || !ndev->ctx || !eth_mac_valid(mac)) return NET_ERR_INVAL;
    eth_device_t *d = (eth_device_t *)ndev->ctx;
    u64 flags = spin_lock_irqsave(&d->hw_lock);
    memcpy(ndev->mac, mac, NET_ETH_ADDR_LEN);
    eth_write_mac(d, ndev->mac);
    spin_unlock_irqrestore(&d->hw_lock, flags);
    return NET_OK;
}

static net_status_t eth_set_mtu(net_device_t *ndev, u16 mtu) {
    if (!ndev || !ndev->ctx) return NET_ERR_NODEV;
    if (mtu < 576u || mtu > ndev->max_mtu) return NET_ERR_RANGE;
    ndev->mtu = mtu;
    return NET_OK;
}

static net_status_t eth_set_vlan(net_device_t *ndev, u16 vlan, bool enabled) {
    if (!ndev || !ndev->ctx || vlan >= 4096u) return NET_ERR_INVAL;
    eth_device_t *d = (eth_device_t *)ndev->ctx;
    u32 idx = vlan >> 5u;
    u32 bit = 1u << (vlan & 31u);
    u64 flags = spin_lock_irqsave(&d->hw_lock);
    u32 v = eth_rd(d, REG_VFTA + idx * 4u);
    if (enabled) v |= bit;
    else v &= ~bit;
    eth_wr(d, REG_VFTA + idx * 4u, v);
    d->has_vlan_filter = true;
    eth_set_rx_mode(d);
    spin_unlock_irqrestore(&d->hw_lock, flags);
    return NET_OK;
}

static void eth_update_link(net_device_t *ndev) {
    if (!ndev || !ndev->ctx) return;
    eth_device_t *d = (eth_device_t *)ndev->ctx;
    u64 flags = spin_lock_irqsave(&d->hw_lock);
    eth_update_link_locked(d);
    spin_unlock_irqrestore(&d->hw_lock, flags);
}

static void eth_format_driver(net_device_t *ndev, char *out, usize out_len) {
    if (!ndev || !ndev->ctx || !out || out_len == 0) return;
    eth_device_t *d = (eth_device_t *)ndev->ctx;
    rabbitbone_buf_out_t bo;
    rabbitbone_buf_init(&bo, out, out_len);
    u32 status = eth_rd(d, REG_STATUS);
    u32 tctl = eth_rd(d, REG_TCTL);
    u32 txdctl = eth_rd(d, REG_TXDCTL);
    u32 tdh = eth_rd(d, REG_TDH);
    u32 tdt = eth_rd(d, REG_TDT);
    rabbitbone_buf_appendf(&bo,
        "    pci=%02x:%02x.%u device=%04x chip=%s irq=%u irq_installed=%u cmd=0x%x mmio=%p size=0x%llx rx_next=%u tx_next=%u tx_clean=%u tx_inflight=%u icr=0x%x status=0x%x tctl=0x%x txdctl=0x%x tdh=%u tdt=%u\n",
        d->pci ? d->pci->bus : 0u, d->pci ? d->pci->device : 0u, d->pci ? d->pci->function : 0u,
        d->pci ? d->pci->device_id : 0u, d->chip_name ? d->chip_name : "unknown", d->irq_line,
        d->irq_installed ? 1u : 0u, d->pci_command, (void *)(uptr)d->mmio_base, (unsigned long long)d->mmio_size,
        d->rx_next, d->tx_next, d->tx_clean, d->tx_inflight, d->last_icr, status, tctl, txdctl, tdh, tdt);
}

static const net_device_ops_t eth_ops = {
    .open = eth_open,
    .close = eth_close,
    .transmit = eth_transmit,
    .poll = eth_poll_netdev,
    .set_promisc = eth_set_promisc,
    .set_allmulti = eth_set_allmulti,
    .set_mac = eth_set_mac_netdev,
    .set_mtu = eth_set_mtu,
    .set_vlan = eth_set_vlan,
    .update_link = eth_update_link,
    .format_driver = eth_format_driver,
};

static bool eth_prepare_pci(const pci_device_t *pd) {
    if (!pd) return false;
    u16 cmd = pci_config_read16(pd->bus, pd->device, pd->function, 0x04);
    cmd |= PCI_COMMAND_MEMORY | PCI_COMMAND_BUS_MASTER;
    if (pd->bars[0].flags & PCI_BAR_IO) cmd |= PCI_COMMAND_IO;
    pci_config_write16(pd->bus, pd->device, pd->function, 0x04, cmd);
    return true;
}

static bool eth_probe_one(const pci_device_t *pd, const eth_adapter_info_t *info) {
    if (!pd || !info || eth_device_count >= ETH_MAX_ADAPTERS) return false;
    const pci_bar_info_t *bar = 0;
    for (u8 i = 0; i < PCI_MAX_BARS; ++i) {
        if (pd->bars[i].base && (pd->bars[i].flags & PCI_BAR_IO) == 0) {
            bar = &pd->bars[i];
            break;
        }
    }
    if (!bar) return false;
    u64 map_size = bar->size ? bar->size : 0x20000u;
    if (!eth_map_mmio(bar->base, map_size)) {
        KLOG(LOG_WARN, "eth", "unable to map mmio for %02x:%02x.%u base=%p size=0x%llx",
             pd->bus, pd->device, pd->function, (void *)(uptr)bar->base, (unsigned long long)map_size);
        return false;
    }
    if (!eth_prepare_pci(pd)) return false;

    eth_device_t *d = &eth_devices[eth_device_count];
    memset(d, 0, sizeof(*d));
    spinlock_init(&d->hw_lock);
    d->pci = pd;
    d->mmio = (volatile u8 *)(uptr)bar->base;
    d->mmio_base = bar->base;
    d->mmio_size = map_size;
    d->irq_line = pd->irq_line;
    d->pci_id = ((u32)pd->vendor_id << 16u) | pd->device_id;
    d->pci_command = pci_config_read16(pd->bus, pd->device, pd->function, 0x04);
    d->chip_name = info->name;
    if (!eth_alloc_rings(d)) {
        eth_free_rings(d);
        KLOG(LOG_WARN, "eth", "ring allocation failed for %02x:%02x.%u", pd->bus, pd->device, pd->function);
        return false;
    }

    d->net.driver = "rabbit-eth";
    d->net.ctx = d;
    d->net.ops = &eth_ops;
    d->net.mtu = 1500u;
    d->net.max_mtu = 1500u;
    d->net.flags = NETDEV_F_BROADCAST | NETDEV_F_MULTICAST;
    d->net.caps = NETDEV_CAP_HW_CSUM | NETDEV_CAP_VLAN | NETDEV_CAP_PROMISC | NETDEV_CAP_ALLMULTI | NETDEV_CAP_MULTICAST | NETDEV_CAP_IRQ | NETDEV_CAP_POLL;
    d->net.tx_queue_len = ETH_TX_RING;
    d->net.rx_queue_len = NET_RX_BACKLOG;

    eth_reset_hw(d);
    eth_read_mac(d);
    net_status_t st = netdev_register(&d->net);
    if (st != NET_OK) {
        eth_free_rings(d);
        KLOG(LOG_WARN, "eth", "netdev register failed: %s", net_status_name(st));
        return false;
    }
    ++eth_device_count;
    st = netdev_open(&d->net);
    if (st != NET_OK) {
        KLOG(LOG_WARN, "eth", "open failed: %s", net_status_name(st));
    }
    return true;
}

void rabbit_eth_init(void) {
    if (eth_initialized) return;
    eth_initialized = true;
    net_init();
    memset(eth_devices, 0, sizeof(eth_devices));
    memset(eth_irq_vectors, 0, sizeof(eth_irq_vectors));
    eth_device_count = 0;

    usize pci_count = pci_device_count();
    for (usize i = 0; i < pci_count && eth_device_count < ETH_MAX_ADAPTERS; ++i) {
        const pci_device_t *pd = pci_get_device(i);
        const eth_adapter_info_t *info = 0;
        if (!eth_supported_pci(pd, &info)) {
            eth_log_unsupported_network(pd);
            continue;
        }
        if (!info->vm_preferred) continue;
        if (!eth_probe_one(pd, info)) {
            KLOG(LOG_WARN, "eth", "probe failed for %02x:%02x.%u vendor=%04x device=%04x", pd->bus, pd->device, pd->function, pd->vendor_id, pd->device_id);
        }
    }
    net_log_devices();
}

void rabbit_eth_poll(void) {
    net_poll_all();
}
