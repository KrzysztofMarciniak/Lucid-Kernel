/* panic.c - fixed core, no recipe dependency on purpose.
 *
 * Can be called from ANY recipe's _pre/_init/_post, including a
 * failed serial/terminal recipe itself -- so it can't rely on any
 * recipe being up. Writes raw to COM1 (0x3F8) via direct port IO,
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
    if (!s) return;
    while (*s) { panic_putc(*s++); }
}

/* label: "field: text\r\n" -- only prints if text is non-NULL. */
static void panic_field(const char *label, const char *text) {
    if (!text) return;
    panic_puts(label);
    panic_puts(text);
    panic_puts("\r\n");
}

void panic_ok(const char *msg) {
    panic_puts("\r\n*** ");
    panic_puts(msg ? msg : "(ok)");
    panic_puts(" ***\r\n");
}

__attribute__((noreturn)) void panic_status(lucid_status st) {
    panic_puts("\r\n*** LUCID KERNEL PANIC ***\r\n");
    panic_field("msg:   ", st.msg ? st.msg : "(no message)");
    panic_field("debug: ", st.debug);
    panic_field("hint:  ", st.hint);
    panic_puts("halted.\r\n");

    __asm__ volatile ("cli");
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

__attribute__((noreturn)) void panic(const char *msg) {
    lucid_status st = { .ok = 1, .msg = msg, .debug = 0, .hint = 0 };
    panic_status(st);
}
