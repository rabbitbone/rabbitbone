#include <aurora/pci.h>
#include <aurora/arch/io.h>
#include <aurora/libc.h>
#include <aurora/log.h>
#include <aurora/console.h>
#include <aurora/kmem.h>

#define PCI_CONFIG_ADDRESS 0xcf8u
#define PCI_CONFIG_DATA    0xcfcu
#define PCI_VENDOR_INVALID 0xffffu

static pci_device_t *pci_devices;
static usize pci_devices_count;
static bool pci_scanned;

static u32 pci_addr(u8 bus, u8 dev, u8 func, u8 offset) {
    return 0x80000000u | ((u32)bus << 16) | ((u32)dev << 11) | ((u32)func << 8) | ((u32)offset & 0xfcu);
}

u32 pci_config_read32(u8 bus, u8 dev, u8 func, u8 offset) {
    outl(PCI_CONFIG_ADDRESS, pci_addr(bus, dev, func, offset));
    return inl(PCI_CONFIG_DATA);
}

u16 pci_config_read16(u8 bus, u8 dev, u8 func, u8 offset) {
    u32 v = pci_config_read32(bus, dev, func, offset);
    return (u16)((v >> ((offset & 2u) * 8u)) & 0xffffu);
}

u8 pci_config_read8(u8 bus, u8 dev, u8 func, u8 offset) {
    u32 v = pci_config_read32(bus, dev, func, offset);
    return (u8)((v >> ((offset & 3u) * 8u)) & 0xffu);
}

void pci_config_write32(u8 bus, u8 dev, u8 func, u8 offset, u32 value) {
    outl(PCI_CONFIG_ADDRESS, pci_addr(bus, dev, func, offset));
    outl(PCI_CONFIG_DATA, value);
}

void pci_config_write16(u8 bus, u8 dev, u8 func, u8 offset, u16 value) {
    u32 aligned = offset & 0xfcu;
    u32 old = pci_config_read32(bus, dev, func, (u8)aligned);
    u32 shift = (u32)(offset & 2u) * 8u;
    u32 mask = 0xffffu << shift;
    u32 next = (old & ~mask) | ((u32)value << shift);
    pci_config_write32(bus, dev, func, (u8)aligned, next);
}

static void pci_decode_bar(pci_device_t *pd, u8 bar_index) {
    u8 off = (u8)(0x10u + bar_index * 4u);
    u32 old = pci_config_read32(pd->bus, pd->device, pd->function, off);
    if (old == 0 || old == 0xffffffffu) return;
    pci_config_write32(pd->bus, pd->device, pd->function, off, 0xffffffffu);
    u32 mask = pci_config_read32(pd->bus, pd->device, pd->function, off);
    pci_config_write32(pd->bus, pd->device, pd->function, off, old);
    io_wait();

    pci_bar_info_t *bar = &pd->bars[bar_index];
    if (old & PCI_BAR_IO) {
        u32 base = old & ~0x3u;
        u32 size_mask = mask & ~0x3u;
        if (size_mask) bar->size = (u64)(~size_mask + 1u) & 0xffffffffu;
        bar->base = base;
        bar->flags = PCI_BAR_IO;
        return;
    }

    u32 type = old & 0x6u;
    u64 base = old & ~0xfull;
    u64 size_mask = mask & ~0xfull;
    bar->flags = (old & 0x8u) ? PCI_BAR_PREFETCH : 0;
    if (type == 0x4u && bar_index + 1u < PCI_MAX_BARS) {
        u32 old_hi = pci_config_read32(pd->bus, pd->device, pd->function, (u8)(off + 4u));
        pci_config_write32(pd->bus, pd->device, pd->function, (u8)(off + 4u), 0xffffffffu);
        u32 mask_hi = pci_config_read32(pd->bus, pd->device, pd->function, (u8)(off + 4u));
        pci_config_write32(pd->bus, pd->device, pd->function, (u8)(off + 4u), old_hi);
        base |= (u64)old_hi << 32;
        size_mask |= (u64)mask_hi << 32;
        bar->flags |= PCI_BAR_MEM64;
    }
    if (size_mask) bar->size = ~size_mask + 1u;
    bar->base = base;
}

static void pci_read_caps(pci_device_t *pd) {
    u16 status = pci_config_read16(pd->bus, pd->device, pd->function, 0x06);
    if (!(status & 0x10u)) return;
    u8 ptr = pci_config_read8(pd->bus, pd->device, pd->function, 0x34) & 0xfcu;
    for (u8 guard = 0; ptr >= 0x40u && guard < 48u && pd->cap_count < PCI_MAX_CAPS; ++guard) {
        u8 id = pci_config_read8(pd->bus, pd->device, pd->function, ptr);
        u8 next = pci_config_read8(pd->bus, pd->device, pd->function, (u8)(ptr + 1u)) & 0xfcu;
        if (id == 0xffu) break;
        pd->caps[pd->cap_count++] = (pci_cap_info_t){ .id = id, .offset = ptr };
        if (next == ptr) break;
        ptr = next;
    }
}

static void pci_add_device(u8 bus, u8 dev, u8 func) {
    if (!pci_devices || pci_devices_count >= PCI_MAX_DEVICES) return;
    u16 vendor = pci_config_read16(bus, dev, func, 0x00);
    if (vendor == PCI_VENDOR_INVALID) return;
    pci_device_t *pd = &pci_devices[pci_devices_count];
    memset(pd, 0, sizeof(*pd));
    pd->bus = bus;
    pd->device = dev;
    pd->function = func;
    pd->vendor_id = vendor;
    pd->device_id = pci_config_read16(bus, dev, func, 0x02);
    pd->revision = pci_config_read8(bus, dev, func, 0x08);
    pd->prog_if = pci_config_read8(bus, dev, func, 0x09);
    pd->subclass = pci_config_read8(bus, dev, func, 0x0a);
    pd->class_code = pci_config_read8(bus, dev, func, 0x0b);
    pd->header_type = pci_config_read8(bus, dev, func, 0x0e);
    pd->irq_line = pci_config_read8(bus, dev, func, 0x3c);
    pd->irq_pin = pci_config_read8(bus, dev, func, 0x3d);
    if ((pd->header_type & 0x7fu) == 0) {
        for (u8 i = 0; i < PCI_MAX_BARS; ++i) {
            pci_decode_bar(pd, i);
            if (pd->bars[i].flags & PCI_BAR_MEM64) ++i;
        }
        pci_read_caps(pd);
    }
    ++pci_devices_count;
}

void pci_init(void) {
    if (!pci_devices) pci_devices = (pci_device_t *)kcalloc(PCI_MAX_DEVICES, sizeof(pci_device_t));
    if (pci_devices) memset(pci_devices, 0, PCI_MAX_DEVICES * sizeof(pci_device_t));
    pci_devices_count = 0;
    for (u16 bus = 0; bus < 256u; ++bus) {
        for (u8 dev = 0; dev < 32u; ++dev) {
            u16 vendor = pci_config_read16((u8)bus, dev, 0, 0x00);
            if (vendor == PCI_VENDOR_INVALID) continue;
            u8 header = pci_config_read8((u8)bus, dev, 0, 0x0e);
            u8 funcs = (header & 0x80u) ? 8u : 1u;
            for (u8 func = 0; func < funcs; ++func) pci_add_device((u8)bus, dev, func);
        }
    }
    pci_scanned = true;
    KLOG(LOG_INFO, "pci", "enumerated devices=%llu", (unsigned long long)pci_devices_count);
}

usize pci_device_count(void) { return pci_devices_count; }
const pci_device_t *pci_get_device(usize index) { return pci_devices && index < pci_devices_count ? &pci_devices[index] : 0; }

const pci_device_t *pci_find_class(u8 class_code, u8 subclass, u8 prog_if, usize nth) {
    if (!pci_devices) return 0;
    for (usize i = 0; i < pci_devices_count; ++i) {
        const pci_device_t *pd = &pci_devices[i];
        if (pd->class_code == class_code && pd->subclass == subclass && pd->prog_if == prog_if) {
            if (nth == 0) return pd;
            --nth;
        }
    }
    return 0;
}

static void append_raw(char *out, usize out_len, usize *used, const char *s) {
    if (!out || !out_len || !used || !s || *used >= out_len) return;
    while (*s && *used + 1u < out_len) out[(*used)++] = *s++;
    out[*used < out_len ? *used : out_len - 1u] = 0;
}

static void appendf(char *out, usize out_len, usize *used, const char *fmt, ...) {
    char tmp[192];
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    kvsnprintf(tmp, sizeof(tmp), fmt, ap);
    __builtin_va_end(ap);
    append_raw(out, out_len, used, tmp);
}

void pci_format_devices(char *out, usize out_len) {
    if (!out || out_len == 0) return;
    out[0] = 0;
    usize used = 0;
    appendf(out, out_len, &used, "pci: count=%llu scanned=%u\n", (unsigned long long)pci_devices_count, pci_scanned ? 1u : 0u);
    if (!pci_devices) return;
    for (usize i = 0; i < pci_devices_count; ++i) {
        const pci_device_t *pd = &pci_devices[i];
        appendf(out, out_len, &used, "  %02x:%02x.%u vendor=%04x device=%04x class=%02x:%02x:%02x irq=%u/%u caps=%u\n",
                pd->bus, pd->device, pd->function, pd->vendor_id, pd->device_id, pd->class_code, pd->subclass, pd->prog_if,
                pd->irq_line, pd->irq_pin, pd->cap_count);
        for (u8 b = 0; b < PCI_MAX_BARS; ++b) {
            const pci_bar_info_t *bar = &pd->bars[b];
            if (!bar->base && !bar->size) continue;
            appendf(out, out_len, &used, "    bar%u base=0x%llx size=0x%llx flags=0x%x\n", b,
                    (unsigned long long)bar->base, (unsigned long long)bar->size, bar->flags);
        }
    }
}
