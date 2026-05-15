#include <rabbitbone/drivers.h>
#include <rabbitbone/arch/io.h>
#include <rabbitbone/libc.h>

#define COM1 0x3f8u
#define UART_LSR_DATA_READY 0x01u
#define UART_LSR_TX_EMPTY   0x20u
#define UART_MCR_DTR        0x01u
#define UART_MCR_RTS        0x02u
#define UART_MCR_OUT2       0x08u
#define UART_MCR_LOOPBACK   0x10u
#define SERIAL_TX_SPIN_LIMIT 4096u

static bool serial_ready;

static u8 uart_read(u16 off) { return inb((u16)(COM1 + off)); }
static void uart_write(u16 off, u8 v) { outb((u16)(COM1 + off), v); }
static bool tx_empty(void) { return (uart_read(5) & UART_LSR_TX_EMPTY) != 0; }

static bool serial_probe_loopback(void) {
    /* Use the UART loopback path instead of trusting LSR.  On machines with no
     * COM1, LSR often reads as 0x00 or 0xff, which used to make every console
     * byte spin for a long timeout. */
    uart_write(4, UART_MCR_LOOPBACK | UART_MCR_DTR | UART_MCR_RTS | UART_MCR_OUT2);
    uart_write(0, 0xaeu);
    for (u32 spin = 0; spin < SERIAL_TX_SPIN_LIMIT; ++spin) {
        if ((uart_read(5) & UART_LSR_DATA_READY) && uart_read(0) == 0xaeu) {
            uart_write(4, UART_MCR_DTR | UART_MCR_RTS | UART_MCR_OUT2);
            return true;
        }
    }
    uart_write(4, 0x00u);
    return false;
}

bool serial_available(void) { return serial_ready; }
bool serial_received(void) { return serial_ready && (uart_read(5) & UART_LSR_DATA_READY) != 0; }

void serial_init(void) {
    serial_ready = false;
    uart_write(1, 0x00u);
    uart_write(3, 0x80u);
    uart_write(0, 0x03u);
    uart_write(1, 0x00u);
    uart_write(3, 0x03u);
    uart_write(2, 0xc7u);
    uart_write(4, UART_MCR_DTR | UART_MCR_RTS | UART_MCR_OUT2);
    serial_ready = serial_probe_loopback();
}

static void serial_write_byte(u8 byte) {
    if (!serial_ready) return;
    for (u32 spin = 0; spin < SERIAL_TX_SPIN_LIMIT; ++spin) {
        if (tx_empty()) {
            uart_write(0, byte);
            return;
        }
    }
    /* Stop penalizing the whole console if the UART disappears or stalls. */
    serial_ready = false;
}

void serial_putc(char c) {
    if (c == '\n') serial_write_byte((u8)'\r');
    serial_write_byte((u8)c);
}

void serial_write_n(const char *s, usize n) {
    if (!s || !serial_ready) return;
    for (usize i = 0; i < n; ++i) {
        char c = s[i];
        if (c == '\n') serial_write_byte((u8)'\r');
        serial_write_byte((u8)c);
        if (!serial_ready) return;
    }
}

void serial_write(const char *s) { if (s) serial_write_n(s, strlen(s)); }
int serial_read(void) { return serial_received() ? (int)uart_read(0) : -1; }
