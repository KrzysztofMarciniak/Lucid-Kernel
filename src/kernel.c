/* kernel.c - Lucid Kernel */

#include <stdint.h>
#include <stddef.h>
#include <gdt.h>
#inlcude <tss.h>

/* --- Serial port (COM1, 0x3F8) --- 
 * Lets us verify the kernel is actually running, independent of
 * whatever's happening with the VGA/graphical display. Requires
 * port I/O (in/out), which C has no syntax for, so tiny inline asm. */

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

#define COM1 0x3F8

static void serial_init(void) {
    outb(COM1 + 1, 0x00); /* disable interrupts */
    outb(COM1 + 3, 0x80); /* enable DLAB (set baud rate divisor) */
    outb(COM1 + 0, 0x03); /* divisor low byte: 38400 baud */
    outb(COM1 + 1, 0x00); /* divisor high byte */
    outb(COM1 + 3, 0x03); /* 8 bits, no parity, one stop bit */
    outb(COM1 + 2, 0xC7); /* enable FIFO, clear, 14-byte threshold */
    outb(COM1 + 4, 0x0B); /* IRQs enabled, RTS/DSR set */
}

static int serial_tx_empty(void) {
    return inb(COM1 + 5) & 0x20;
}

static void serial_putc(char c) {
    while (!serial_tx_empty());
    outb(COM1, (uint8_t)c);
}

void serial_print(const char* s) {
    for (size_t i = 0; s[i] != '\0'; i++)
        serial_putc(s[i]);
}

void serial_print_hex(uint32_t n) {
    const char* hex = "0123456789ABCDEF";
    char buf[11] = "0x00000000";
    for (int i = 9; i >= 2; i--) {
        buf[i] = hex[n & 0xF];
        n >>= 4;
    }
    serial_print(buf);
}

/* --- VGA text mode: see terminal.c --- */
extern void terminal_initialize(void);
extern void terminal_writestring(const char* s);

extern void gdt_install(void);
extern void idt_install(void);
extern void keyboard_install(void);
extern void print_boot_stats(uint32_t mb_info_addr);

void kernel_main(uint32_t mb_info_addr) {
    serial_init();
    serial_print("kernel_main() reached, serial is alive\n");

    gdt_install();
    serial_print("GDT installed\n");

    idt_install();
    serial_print("IDT installed\n");

    __asm__ volatile ("sti");
    serial_print("interrupts enabled\n");

    __asm__ volatile ("int $0x3"); /* breakpoint, should return cleanly */
    serial_print("survived a test interrupt (int3)\n");

    keyboard_install();
    serial_print("keyboard driver installed\n");

    terminal_initialize();
    print_boot_stats(mb_info_addr);
    terminal_writestring("\ntype something:\n");

    serial_print("VGA buffer written, entering idle loop\n");

    while (1) {
        __asm__ volatile ("hlt");
    }
}
