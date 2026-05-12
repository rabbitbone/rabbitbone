#include <aurora/drivers.h>
#include <aurora/arch/io.h>

#define PIC1 0x20
#define PIC2 0xa0
#define PIC1_COMMAND PIC1
#define PIC1_DATA (PIC1 + 1)
#define PIC2_COMMAND PIC2
#define PIC2_DATA (PIC2 + 1)
#define PIC_EOI 0x20

void pic_remap(u8 offset1, u8 offset2) {
    u8 a1 = inb(PIC1_DATA);
    u8 a2 = inb(PIC2_DATA);
    outb(PIC1_COMMAND, 0x11); io_wait();
    outb(PIC2_COMMAND, 0x11); io_wait();
    outb(PIC1_DATA, offset1); io_wait();
    outb(PIC2_DATA, offset2); io_wait();
    outb(PIC1_DATA, 4); io_wait();
    outb(PIC2_DATA, 2); io_wait();
    outb(PIC1_DATA, 0x01); io_wait();
    outb(PIC2_DATA, 0x01); io_wait();
    outb(PIC1_DATA, a1);
    outb(PIC2_DATA, a2);
}

void pic_send_eoi(u8 irq) {
    if (irq >= 16) return;
    if (irq >= 8) outb(PIC2_COMMAND, PIC_EOI);
    outb(PIC1_COMMAND, PIC_EOI);
}

void pic_set_mask(u8 irq) {
    if (irq >= 16) return;
    u16 port = irq < 8 ? PIC1_DATA : PIC2_DATA;
    if (irq >= 8) irq -= 8;
    outb(port, (u8)(inb(port) | (u8)(1u << irq)));
}

void pic_clear_mask(u8 irq) {
    if (irq >= 16) return;
    u16 port = irq < 8 ? PIC1_DATA : PIC2_DATA;
    if (irq >= 8) irq -= 8;
    outb(port, (u8)(inb(port) & (u8)~(1u << irq)));
}
