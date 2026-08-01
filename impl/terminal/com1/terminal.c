#include "recipes/terminal.h"
#include "recipes/status.h"

#define COM1 0x3F8

static inline void outb(unsigned short port, unsigned char value)
{
    __asm__ volatile (
        "outb %0, %1"
        :
        : "a"(value), "Nd"(port)
    );
}

static inline unsigned char inb(unsigned short port)
{
    unsigned char value;

    __asm__ volatile (
        "inb %1, %0"
        : "=a"(value)
        : "Nd"(port)
    );

    return value;
}

static void serial_init(void)
{
    /* Disable interrupts */
    outb(COM1 + 1, 0x00);

    /* Enable DLAB */
    outb(COM1 + 3, 0x80);

    /* Baud divisor: 115200 / 115200 = 1 */
    outb(COM1 + 0, 0x01);
    outb(COM1 + 1, 0x00);

    /* 8 bits, no parity, one stop bit */
    outb(COM1 + 3, 0x03);

    /* Enable FIFO, clear buffers, 14-byte threshold */
    outb(COM1 + 2, 0xC7);

    /* RTS/DSR */
    outb(COM1 + 4, 0x0B);
}

static int serial_ready(void)
{
    return inb(COM1 + 5) & 0x20;
}

void terminal_putc(char c)
{
    while (!serial_ready())
        ;

    outb(COM1, c);
}

void terminal_write(const char *s)
{
    while (*s) {
        if (*s == '\n')
            terminal_putc('\r');

        terminal_putc(*s++);
    }
}

lucid_status terminal_pre(void)
{
    return LUCID_OK_MSG("Initializing COM1 serial port");
}

lucid_status terminal_init(void)
{
    serial_init();

    return LUCID_OK_MSG(
        "COM1 serial port initialized (115200 baud, 8 data bits, no parity, 1 stop bit)"
    );
}

lucid_status terminal_post(void)
{
    return LUCID_OK_MSG("COM1 serial port ready for output");
}
