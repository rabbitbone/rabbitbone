#include <rabbitbone/drivers.h>
#include <rabbitbone/arch/io.h>
#include <rabbitbone/libc.h>
#include <rabbitbone/kmem.h>

#define COM1 0x3f8u
#define UART_LSR_DATA_READY 0x01u
#define UART_LSR_TX_EMPTY   0x20u
#define UART_MCR_DTR        0x01u
#define UART_MCR_RTS        0x02u
#define UART_MCR_OUT2       0x08u
#define UART_MCR_LOOPBACK   0x10u
#define SERIAL_TX_SPIN_LIMIT 4096u
#define SERIAL_EARLY_TX_RING_SIZE 4096u
#define SERIAL_HEAP_TX_RING_SIZE 262144u
#define SERIAL_DRAIN_BUDGET 0u
#define SERIAL_POLL_BUDGET 128u

static bool serial_ready;
static u8 serial_tx_ring_static[SERIAL_EARLY_TX_RING_SIZE];
static u8 *serial_tx_ring = serial_tx_ring_static;
static u32 serial_tx_capacity = SERIAL_EARLY_TX_RING_SIZE;
static u32 serial_tx_head;
static u32 serial_tx_tail;
static bool serial_tx_overflowed;

static u8 uart_read(u16 off) { return inb((u16)(COM1 + off)); }
static void uart_write(u16 off, u8 v) { outb((u16)(COM1 + off), v); }
static bool tx_empty(void) { return (uart_read(5) & UART_LSR_TX_EMPTY) != 0; }

static bool serial_probe_loopback(void) {
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

static u32 ring_next(u32 idx) {
    u32 capacity = serial_tx_capacity ? serial_tx_capacity : SERIAL_EARLY_TX_RING_SIZE;
    return (idx + 1u) % capacity;
}

static bool ring_empty(void) { return serial_tx_head == serial_tx_tail; }

static u32 ring_count(void) {
    if (serial_tx_head >= serial_tx_tail) return serial_tx_head - serial_tx_tail;
    return serial_tx_capacity - serial_tx_tail + serial_tx_head;
}

static void serial_queue_byte(u8 byte) {
    u32 next = ring_next(serial_tx_head);
    if (next == serial_tx_tail) {
        serial_tx_tail = ring_next(serial_tx_tail);
        serial_tx_overflowed = true;
    }
    serial_tx_ring[serial_tx_head] = byte;
    serial_tx_head = next;
}

static void serial_drain(u32 budget) {
    if (!serial_ready) return;
    while (budget && !ring_empty()) {
        if (!tx_empty()) return;
        u32 burst = budget < 16u ? budget : 16u;
        while (burst-- && !ring_empty()) {
            uart_write(0, serial_tx_ring[serial_tx_tail]);
            serial_tx_tail = ring_next(serial_tx_tail);
            --budget;
        }
    }
}

void serial_enable_heap_ring(void) {
    if (serial_tx_ring != serial_tx_ring_static) return;
    u8 *heap_ring = (u8 *)kmalloc(SERIAL_HEAP_TX_RING_SIZE);
    if (!heap_ring) return;

    u32 count = ring_count();
    if (count >= SERIAL_HEAP_TX_RING_SIZE) count = SERIAL_HEAP_TX_RING_SIZE - 1u;
    u32 cursor = serial_tx_tail;
    for (u32 i = 0; i < count; ++i) {
        heap_ring[i] = serial_tx_ring_static[cursor];
        cursor = ring_next(cursor);
    }

    serial_tx_ring = heap_ring;
    serial_tx_capacity = SERIAL_HEAP_TX_RING_SIZE;
    serial_tx_tail = 0;
    serial_tx_head = count;
}

void serial_poll(void) { serial_drain(SERIAL_POLL_BUDGET); }

bool serial_available(void) { return serial_ready; }
bool serial_received(void) { return serial_ready && (uart_read(5) & UART_LSR_DATA_READY) != 0; }

void serial_init(void) {
    serial_ready = false;
    serial_tx_head = 0;
    serial_tx_tail = 0;
    serial_tx_overflowed = false;
    uart_write(1, 0x00u);
    uart_write(3, 0x80u);
    uart_write(0, 0x01u);
    uart_write(1, 0x00u);
    uart_write(3, 0x03u);
    uart_write(2, 0xc7u);
    uart_write(4, UART_MCR_DTR | UART_MCR_RTS | UART_MCR_OUT2);
    serial_ready = serial_probe_loopback();
}

void serial_putc(char c) {
    if (!serial_ready) return;
    if (c == '\n') serial_queue_byte((u8)'\r');
    serial_queue_byte((u8)c);
    serial_drain(SERIAL_DRAIN_BUDGET);
}

void serial_write_n(const char *s, usize n) {
    if (!s || !serial_ready) return;
    for (usize i = 0; i < n; ++i) {
        char c = s[i];
        if (c == '\n') serial_queue_byte((u8)'\r');
        serial_queue_byte((u8)c);
    }
    serial_drain(SERIAL_DRAIN_BUDGET);
}

void serial_write(const char *s) { if (s) serial_write_n(s, strlen(s)); }
int serial_read(void) {
    serial_drain(SERIAL_DRAIN_BUDGET);
    return serial_received() ? (int)uart_read(0) : -1;
}
