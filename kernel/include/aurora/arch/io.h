#ifndef AURORA_ARCH_IO_H
#define AURORA_ARCH_IO_H
#include <aurora/types.h>

static inline void outb(u16 port, u8 value) { __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port)); }
static inline void outw(u16 port, u16 value) { __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port)); }
static inline void outl(u16 port, u32 value) { __asm__ volatile("outl %0, %1" : : "a"(value), "Nd"(port)); }
static inline u8 inb(u16 port) { u8 v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port)); return v; }
static inline u16 inw(u16 port) { u16 v; __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(port)); return v; }
static inline u32 inl(u16 port) { u32 v; __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(port)); return v; }
static inline void io_wait(void) { outb(0x80, 0); }
static inline void cpu_hlt(void) { __asm__ volatile("hlt" ::: "memory"); }
static inline void cpu_cli(void) { __asm__ volatile("cli" ::: "memory"); }
static inline void cpu_sti(void) { __asm__ volatile("sti" ::: "memory"); }
static inline u64 read_cr2(void) { u64 v; __asm__ volatile("mov %%cr2, %0" : "=r"(v)); return v; }
static inline u64 read_cr3(void) { u64 v; __asm__ volatile("mov %%cr3, %0" : "=r"(v)); return v; }

#endif
