/* terminal.c - terminal :: vga_text
 * profile: standard, reliable, ~250 bytes .text
 * Direct writes to the 0xB8000 text-mode buffer. No scrolling
 * acceleration, no color config -- fixed light-gray-on-black.
 * pick this when: you just need boot messages visible on real VGA
 * hardware or in QEMU's default display; no serial console assumed.
 */
#include "recipes/terminal.h"
#include "stdint.h"
#include "stddef.h"

#define VGA_WIDTH  80
#define VGA_HEIGHT 25
#define VGA_ATTR   0x07 /* light gray on black */

static volatile uint16_t *const vga_buf = (uint16_t *)0xB8000;
static size_t cursor_row;
static size_t cursor_col;

static void scroll_if_needed(void) {
    if (cursor_row < VGA_HEIGHT) return;

    for (size_t r = 1; r < VGA_HEIGHT; r++) {
        for (size_t c = 0; c < VGA_WIDTH; c++) {
            vga_buf[(r - 1) * VGA_WIDTH + c] = vga_buf[r * VGA_WIDTH + c];
        }
    }
    for (size_t c = 0; c < VGA_WIDTH; c++) {
        vga_buf[(VGA_HEIGHT - 1) * VGA_WIDTH + c] = ((uint16_t)VGA_ATTR << 8) | ' ';
    }
    cursor_row = VGA_HEIGHT - 1;
}

lucid_status terminal_pre(void) {
    return LUCID_OK;
}

lucid_status terminal_init(void) {
    cursor_row = 0;
    cursor_col = 0;
    for (size_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga_buf[i] = ((uint16_t)VGA_ATTR << 8) | ' ';
    }
    return LUCID_OK;
}

lucid_status terminal_post(void) {
    return LUCID_OK;
}

void terminal_putc(char c) {
    if (c == '\n') {
        cursor_col = 0;
        cursor_row++;
        scroll_if_needed();
        return;
    }

    vga_buf[cursor_row * VGA_WIDTH + cursor_col] = ((uint16_t)VGA_ATTR << 8) | (uint8_t)c;
    cursor_col++;
    if (cursor_col >= VGA_WIDTH) {
        cursor_col = 0;
        cursor_row++;
        scroll_if_needed();
    }
}

void terminal_write(const char *s) {
    while (*s) { terminal_putc(*s++); }
}
