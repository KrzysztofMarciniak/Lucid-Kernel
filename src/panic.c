/* panic.c - fixed core, no recipe dependency on purpose.
 *
 * panic() can be called from ANY recipe's _pre/_init/_post, including
 * a failed serial recipe itself -- so it can't rely on the serial
 * recipe being up. It writes raw to COM1 (0x3F8) via direct port IO,
 * bypassing whatever recipe/impl is or isn't installed.
 */
#include "panic.h"
#include "stdint.h"

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

#define COM1 0x3F8

static void panic_putc(char c) {
    while (!(inb(COM1 + 5) & 0x20)) { }
    outb(COM1, (uint8_t)c);
}

static void panic_puts(const char *s) {
    if (!s) { s = "(no message)"; }
    while (*s) { panic_putc(*s++); }
}

void panic_ok(const char *msg) {
    panic_puts("\r\n*** ");
    panic_puts(msg);
    panic_puts(" ***\r\n");
}

__attribute__((noreturn)) void panic(const char *msg) {
    panic_puts("\r\n*** LUCID KERNEL PANIC ***\r\n");
    panic_puts(msg);
    panic_puts("\r\nhalted.\r\n");

    __asm__ volatile ("cli");
    for (;;) {
        __asm__ volatile ("hlt");
    }
}
