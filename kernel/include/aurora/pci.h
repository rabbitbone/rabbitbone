#ifndef AURORA_PCI_H
#define AURORA_PCI_H
#include <aurora/types.h>
#if defined(__cplusplus)
extern "C" {
#endif

#define PCI_MAX_DEVICES 64u
#define PCI_MAX_BARS 6u
#define PCI_MAX_CAPS 8u

#define PCI_BAR_IO 0x1u
#define PCI_BAR_MEM64 0x4u
#define PCI_BAR_PREFETCH 0x8u

typedef struct pci_bar_info {
    u64 base;
    u64 size;
    u32 flags;
} pci_bar_info_t;

typedef struct pci_cap_info {
    u8 id;
    u8 offset;
} pci_cap_info_t;

typedef struct pci_device {
    u8 bus;
    u8 device;
    u8 function;
    u16 vendor_id;
    u16 device_id;
    u8 class_code;
    u8 subclass;
    u8 prog_if;
    u8 revision;
    u8 header_type;
    u8 irq_line;
    u8 irq_pin;
    pci_bar_info_t bars[PCI_MAX_BARS];
    pci_cap_info_t caps[PCI_MAX_CAPS];
    u8 cap_count;
} pci_device_t;

void pci_init(void);
usize pci_device_count(void);
const pci_device_t *pci_get_device(usize index);
const pci_device_t *pci_find_class(u8 class_code, u8 subclass, u8 prog_if, usize nth);
u8 pci_config_read8(u8 bus, u8 dev, u8 func, u8 offset);
u16 pci_config_read16(u8 bus, u8 dev, u8 func, u8 offset);
u32 pci_config_read32(u8 bus, u8 dev, u8 func, u8 offset);
void pci_config_write16(u8 bus, u8 dev, u8 func, u8 offset, u16 value);
void pci_config_write32(u8 bus, u8 dev, u8 func, u8 offset, u32 value);
void pci_format_devices(char *out, usize out_len);

#if defined(__cplusplus)
}
#endif
#endif
