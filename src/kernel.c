/* kernel.c - Lucid Kernel */

#include <stdint.h>
#include <stddef.h>

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

static void serial_print(const char* s) {
    for (size_t i = 0; s[i] != '\0'; i++)
        serial_putc(s[i]);
}

/* --- VGA text mode (0xB8000) --- */
static uint16_t* const VGA_BUFFER = (uint16_t*) 0xB8000;
static const int VGA_WIDTH = 80;
static const int VGA_HEIGHT = 25;

static inline uint16_t vga_entry(char c, uint8_t color) {
    return (uint16_t)c | (uint16_t)color << 8;
}

void kernel_main(void) {
    serial_init();
    serial_print("kernel_main() reached, serial is alive\n");

    const uint8_t color = 0x0A; /* light green on black */
    const char* msg = "Lucid Kernel booted!";

    /* clear screen */
    for (int y = 0; y < VGA_HEIGHT; y++)
        for (int x = 0; x < VGA_WIDTH; x++)
            VGA_BUFFER[y * VGA_WIDTH + x] = vga_entry(' ', color);

    /* print message on first row */
    for (size_t i = 0; msg[i] != '\0'; i++)
        VGA_BUFFER[i] = vga_entry(msg[i], color);

    serial_print("VGA buffer written\n");

    while (1) {
        __asm__ volatile ("hlt");
    }
}
