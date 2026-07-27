/* terminal.c - VGA text-mode terminal
 *
 * Factored out of kernel.c now that we need real cursor-tracked output
 * (the keyboard driver needs somewhere to put typed characters, not
 * just a single static boot message).
 */

#include <stdint.h>
#include <stddef.h>

static uint16_t* const VGA_BUFFER = (uint16_t*) 0xB8000;
static const int VGA_WIDTH = 80;
static const int VGA_HEIGHT = 25;

static int term_row = 0;
static int term_col = 0;
static uint8_t term_color = 0x1F; /* white text on blue background */

static inline uint16_t vga_entry(char c, uint8_t color) {
    return (uint16_t)c | (uint16_t)color << 8;
}

void terminal_setcolor(uint8_t color) {
    term_color = color;
}

void terminal_initialize(void) {
    for (int y = 0; y < VGA_HEIGHT; y++)
        for (int x = 0; x < VGA_WIDTH; x++)
            VGA_BUFFER[y * VGA_WIDTH + x] = vga_entry(' ', term_color);
    term_row = 0;
    term_col = 0;
}

static void terminal_scroll(void) {
    for (int y = 1; y < VGA_HEIGHT; y++)
        for (int x = 0; x < VGA_WIDTH; x++)
            VGA_BUFFER[(y - 1) * VGA_WIDTH + x] = VGA_BUFFER[y * VGA_WIDTH + x];

    for (int x = 0; x < VGA_WIDTH; x++)
        VGA_BUFFER[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = vga_entry(' ', term_color);

    term_row = VGA_HEIGHT - 1;
}

static void terminal_newline(void) {
    term_col = 0;
    term_row++;
    if (term_row >= VGA_HEIGHT) terminal_scroll();
}

void terminal_putchar(char c) {
    if (c == '\n') {
        terminal_newline();
        return;
    }

    if (c == '\b') {
        if (term_col > 0) {
            term_col--;
        } else if (term_row > 0) {
            term_row--;
            term_col = VGA_WIDTH - 1;
        }
        VGA_BUFFER[term_row * VGA_WIDTH + term_col] = vga_entry(' ', term_color);
        return;
    }

    VGA_BUFFER[term_row * VGA_WIDTH + term_col] = vga_entry(c, term_color);
    term_col++;
    if (term_col >= VGA_WIDTH) terminal_newline();
}

void terminal_writestring(const char* s) {
    for (size_t i = 0; s[i] != '\0'; i++)
        terminal_putchar(s[i]);
}
