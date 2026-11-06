#include "recipes/terminal.h"
#include "recipes/status.h"
#include "stdint.h"
#include "stddef.h"
#include "multiboot.h"

#define COM1 0x3F8
#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_ATTR 0x07
#define CURIE_WIDTH 6
#define CURIE_HEIGHT 12

static volatile uint16_t *const vga_buf = (uint16_t *)0xB8000;
extern const uint8_t curie_font[128][CURIE_HEIGHT];
static size_t cursor_row;
static size_t cursor_col;
static volatile uint8_t *framebuffer;
static uint32_t framebuffer_phys;
static uint32_t framebuffer_length;
static uint32_t framebuffer_pitch;
static uint32_t framebuffer_width;
static uint32_t framebuffer_height;
static uint32_t framebuffer_foreground;
static uint32_t framebuffer_background;
static uint32_t cursor_saved_pixels[CURIE_WIDTH * CURIE_HEIGHT];
static uint16_t cursor_saved_cell;
static size_t cursor_saved_row;
static size_t cursor_saved_col;
static const char *framebuffer_status =
    "GRUB did not provide a framebuffer; using VGA output";
static int left_shift;
static int right_shift;
static int left_control;
static int right_control;
static int extended_scancode;
static int serial_after_cr;
static int serial_available;
static int serial_initialized;
static int serial_escape_state;
static uint32_t serial_csi_parameters[2];
static unsigned int serial_csi_parameter_count;
static int serial_csi_invalid;
static int serial_pending_valid;
static char serial_pending_char;
static int display_lambda_prefix;
static int display_escape_state;
static uint32_t display_csi_parameter;
static int terminal_cursor_visible;

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void serial_init(void)
{
    if (serial_initialized) return;
    serial_initialized = 1;

    /* A missing UART reads all ones, which otherwise looks like RX data. */
    if (inb(COM1 + 5) == 0xFF) {
        serial_available = 0;
        return;
    }
    serial_available = 1;

    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1, 0x01);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
}

static void vga_scroll(void)
{
    for (size_t row = 1; row < VGA_HEIGHT; row++) {
        for (size_t col = 0; col < VGA_WIDTH; col++) {
            vga_buf[(row - 1) * VGA_WIDTH + col] =
                vga_buf[row * VGA_WIDTH + col];
        }
    }

    for (size_t col = 0; col < VGA_WIDTH; col++) {
        vga_buf[(VGA_HEIGHT - 1) * VGA_WIDTH + col] =
            ((uint16_t)VGA_ATTR << 8) | ' ';
    }
    cursor_row = VGA_HEIGHT - 1;
}

static void vga_put_ascii(char c)
{
    if (c == '\r') {
        cursor_col = 0;
        return;
    }
    if (c == '\n') {
        cursor_col = 0;
        cursor_row++;
    } else if (c == '\b') {
        if (cursor_col > 0) cursor_col--;
        return;
    } else {
        vga_buf[cursor_row * VGA_WIDTH + cursor_col] =
            ((uint16_t)VGA_ATTR << 8) | (uint8_t)c;
        cursor_col++;
        if (cursor_col == VGA_WIDTH) {
            cursor_col = 0;
            cursor_row++;
        }
    }

    if (cursor_row >= VGA_HEIGHT) {
        vga_scroll();
    }
}

static void framebuffer_pixel(uint32_t x, uint32_t y, uint32_t color)
{
    volatile uint32_t *row =
        (volatile uint32_t *)(framebuffer + y * framebuffer_pitch);
    row[x] = color;
}

static void framebuffer_clear(void)
{
    for (uint32_t y = 0; y < framebuffer_height; y++) {
        for (uint32_t x = 0; x < framebuffer_width; x++) {
            framebuffer_pixel(x, y, framebuffer_background);
        }
    }
}

static void framebuffer_scroll(void)
{
    if (framebuffer_height <= CURIE_HEIGHT) {
        framebuffer_clear();
        cursor_row = 0;
        return;
    }

    for (uint32_t y = CURIE_HEIGHT; y < framebuffer_height; y++) {
        for (uint32_t x = 0; x < framebuffer_width; x++) {
            volatile uint32_t *row =
                (volatile uint32_t *)(framebuffer + y * framebuffer_pitch);
            framebuffer_pixel(x, y - CURIE_HEIGHT, row[x]);
        }
    }
    for (uint32_t y = framebuffer_height - CURIE_HEIGHT;
         y < framebuffer_height;
         y++) {
        for (uint32_t x = 0; x < framebuffer_width; x++) {
            framebuffer_pixel(x, y, framebuffer_background);
        }
    }
    cursor_row = framebuffer_height / CURIE_HEIGHT - 1;
}

static void framebuffer_draw_glyph(size_t row, size_t col, uint8_t glyph)
{
    if (glyph >= 128) glyph = '?';

    for (uint32_t y = 0; y < CURIE_HEIGHT; y++) {
        uint8_t bits = curie_font[glyph][y];
        for (uint32_t x = 0; x < CURIE_WIDTH; x++) {
            uint32_t color = (bits & (1u << (CURIE_WIDTH - 1 - x)))
                ? framebuffer_foreground
                : framebuffer_background;
            framebuffer_pixel(
                (uint32_t)(col * CURIE_WIDTH + x),
                (uint32_t)(row * CURIE_HEIGHT + y),
                color
            );
        }
    }
}

static void framebuffer_put_ascii(char c)
{
    size_t columns = framebuffer_width / CURIE_WIDTH;
    size_t rows = framebuffer_height / CURIE_HEIGHT;

    if (c == '\r') {
        cursor_col = 0;
        return;
    }
    if (c == '\n') {
        cursor_col = 0;
        cursor_row++;
    } else if (c == '\b') {
        if (cursor_col > 0) cursor_col--;
        return;
    } else {
        framebuffer_draw_glyph(cursor_row, cursor_col, (uint8_t)c);
        cursor_col++;
        if (cursor_col >= columns) {
            cursor_col = 0;
            cursor_row++;
        }
    }

    if (cursor_row >= rows) {
        framebuffer_scroll();
    }
}

static void display_put_ascii(char c)
{
    if (framebuffer) framebuffer_put_ascii(c);
    else vga_put_ascii(c);
}

static void display_clear_to_end_of_line(void)
{
    if (framebuffer) {
        size_t columns = framebuffer_width / CURIE_WIDTH;
        size_t rows = framebuffer_height / CURIE_HEIGHT;
        if (cursor_row >= rows) return;
        for (size_t col = cursor_col; col < columns; col++) {
            framebuffer_draw_glyph(cursor_row, col, ' ');
        }
        return;
    }

    if (cursor_row >= VGA_HEIGHT) return;
    for (size_t col = cursor_col; col < VGA_WIDTH; col++) {
        vga_buf[cursor_row * VGA_WIDTH + col] =
            ((uint16_t)VGA_ATTR << 8) | ' ';
    }
}

static void display_clear_to_end_of_screen(void)
{
    if (framebuffer) {
        size_t columns = framebuffer_width / CURIE_WIDTH;
        size_t rows = framebuffer_height / CURIE_HEIGHT;
        for (size_t row = cursor_row; row < rows; row++) {
            size_t start = row == cursor_row ? cursor_col : 0;
            for (size_t col = start; col < columns; col++) {
                framebuffer_draw_glyph(row, col, ' ');
            }
        }
        return;
    }

    for (size_t row = cursor_row; row < VGA_HEIGHT; row++) {
        size_t start = row == cursor_row ? cursor_col : 0;
        for (size_t col = start; col < VGA_WIDTH; col++) {
            vga_buf[row * VGA_WIDTH + col] =
                ((uint16_t)VGA_ATTR << 8) | ' ';
        }
    }
}

static void display_csi_execute(uint8_t final)
{
    if (final == 'H' || final == 'f') {
        cursor_row = 0;
        cursor_col = 0;
    } else if (final == 'J') {
        if (display_csi_parameter == 2) {
            if (framebuffer) framebuffer_clear();
            else {
                for (size_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
                    vga_buf[i] = ((uint16_t)VGA_ATTR << 8) | ' ';
                }
            }
        } else {
            display_clear_to_end_of_screen();
        }
    } else if (final == 'K') {
        display_clear_to_end_of_line();
    }
}

static void display_putc(char c)
{
    uint8_t byte = (uint8_t)c;
    if (display_escape_state == 1) {
        display_escape_state = 0;
        if (byte == '[') {
            display_escape_state = 2;
            display_csi_parameter = 0;
        }
        return;
    }
    if (display_escape_state == 2) {
        if (byte >= '0' && byte <= '9') {
            uint32_t digit = (uint32_t)(byte - '0');
            if (display_csi_parameter <= (999u - digit) / 10u) {
                display_csi_parameter =
                    display_csi_parameter * 10u + digit;
            }
            return;
        }
        display_escape_state = 0;
        if (byte >= 0x40 && byte <= 0x7E) {
            display_csi_execute(byte);
        }
        return;
    }
    if (byte == 0x1B) {
        display_escape_state = 1;
        return;
    }
    if (display_lambda_prefix) {
        display_lambda_prefix = 0;
        display_put_ascii(byte == 0xBB ? 'l' : '?');
        return;
    }
    if (byte == 0xCE) {
        display_lambda_prefix = 1;
        return;
    }
    if (byte >= 0x80) {
        display_put_ascii('?');
        return;
    }
    display_put_ascii(c);
}

static void serial_putc(char c)
{
    if (!serial_available) return;
    while (!(inb(COM1 + 5) & 0x20)) {
    }
    outb(COM1, (uint8_t)c);
}

void terminal_clear(void)
{
    serial_init();
    terminal_cursor_hide();
    cursor_row = 0;
    cursor_col = 0;
    display_lambda_prefix = 0;
    display_escape_state = 0;
    display_csi_parameter = 0;
    if (framebuffer) {
        framebuffer_clear();
    } else {
        for (size_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
            vga_buf[i] = ((uint16_t)VGA_ATTR << 8) | ' ';
        }
    }

    if (serial_available) {
        serial_putc('\x1B');
        serial_putc('[');
        serial_putc('2');
        serial_putc('J');
        serial_putc('\x1B');
        serial_putc('[');
        serial_putc('H');
    }
}

void terminal_cursor_show(void)
{
    if (terminal_cursor_visible) return;
    if (framebuffer) {
        size_t columns = framebuffer_width / CURIE_WIDTH;
        size_t rows = framebuffer_height / CURIE_HEIGHT;
        if (cursor_row >= rows || cursor_col >= columns) return;
        cursor_saved_row = cursor_row;
        cursor_saved_col = cursor_col;
        size_t index = 0;
        for (uint32_t y = 0; y < CURIE_HEIGHT; y++) {
            volatile uint32_t *row =
                (volatile uint32_t *)(
                    framebuffer + (cursor_row * CURIE_HEIGHT + y) *
                    framebuffer_pitch
                );
            for (uint32_t x = 0; x < CURIE_WIDTH; x++) {
                volatile uint32_t *pixel = &row[
                    cursor_col * CURIE_WIDTH + x
                ];
                cursor_saved_pixels[index++] = *pixel;
                framebuffer_pixel(
                    (uint32_t)(cursor_col * CURIE_WIDTH + x),
                    (uint32_t)(cursor_row * CURIE_HEIGHT + y),
                    *pixel == framebuffer_background
                        ? framebuffer_foreground
                        : framebuffer_background
                );
            }
        }
    } else {
        if (cursor_row >= VGA_HEIGHT || cursor_col >= VGA_WIDTH) return;
        size_t index = cursor_row * VGA_WIDTH + cursor_col;
        cursor_saved_cell = vga_buf[index];
        uint8_t attribute = (uint8_t)(cursor_saved_cell >> 8);
        uint8_t foreground = attribute & 0x0F;
        uint8_t background = attribute >> 4;
        vga_buf[index] = (uint16_t)(
            ((uint16_t)((foreground << 4) | background) << 8) |
            (cursor_saved_cell & 0xFF)
        );
    }
    terminal_cursor_visible = 1;
}

void terminal_cursor_hide(void)
{
    if (!terminal_cursor_visible) return;
    if (framebuffer) {
        size_t index = 0;
        for (uint32_t y = 0; y < CURIE_HEIGHT; y++) {
            for (uint32_t x = 0; x < CURIE_WIDTH; x++) {
                framebuffer_pixel(
                    (uint32_t)(cursor_saved_col * CURIE_WIDTH + x),
                    (uint32_t)(cursor_saved_row * CURIE_HEIGHT + y),
                    cursor_saved_pixels[index++]
                );
            }
        }
    } else {
        vga_buf[cursor_saved_row * VGA_WIDTH + cursor_saved_col] =
            cursor_saved_cell;
    }
    terminal_cursor_visible = 0;
}

void terminal_putc(char c)
{
    display_putc(c);

    if (c == '\n') {
        serial_putc('\r');
    }
    serial_putc(c);
}

void terminal_write(const char *s)
{
    while (*s) {
        terminal_putc(*s++);
    }
}

static int scancode_to_ascii(uint8_t code)
{
    static const char normal[0x40] = {
        [0x01] = '\x1B',
        [0x01] = '\x1B',
        [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4',
        [0x06] = '5', [0x07] = '6', [0x08] = '7', [0x09] = '8',
        [0x0A] = '9', [0x0B] = '0', [0x0C] = '-', [0x0D] = '=',
        [0x0E] = '\b', [0x0F] = '\t',
        [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r',
        [0x14] = 't', [0x15] = 'y', [0x16] = 'u', [0x17] = 'i',
        [0x18] = 'o', [0x19] = 'p', [0x1A] = '[', [0x1B] = ']',
        [0x1C] = '\n', [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd',
        [0x21] = 'f', [0x22] = 'g', [0x23] = 'h', [0x24] = 'j',
        [0x25] = 'k', [0x26] = 'l', [0x27] = ';', [0x28] = '\'',
        [0x29] = '`', [0x2B] = '\\', [0x2C] = 'z', [0x2D] = 'x',
        [0x2E] = 'c', [0x2F] = 'v', [0x30] = 'b', [0x31] = 'n',
        [0x32] = 'm', [0x33] = ',', [0x34] = '.', [0x35] = '/',
        [0x39] = ' '
    };
    static const char shifted[0x40] = {
        [0x01] = '\x1B',
        [0x01] = '\x1B',
        [0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$',
        [0x06] = '%', [0x07] = '^', [0x08] = '&', [0x09] = '*',
        [0x0A] = '(', [0x0B] = ')', [0x0C] = '_', [0x0D] = '+',
        [0x0E] = '\b', [0x0F] = '\t',
        [0x10] = 'Q', [0x11] = 'W', [0x12] = 'E', [0x13] = 'R',
        [0x14] = 'T', [0x15] = 'Y', [0x16] = 'U', [0x17] = 'I',
        [0x18] = 'O', [0x19] = 'P', [0x1A] = '{', [0x1B] = '}',
        [0x1C] = '\n', [0x1E] = 'A', [0x1F] = 'S', [0x20] = 'D',
        [0x21] = 'F', [0x22] = 'G', [0x23] = 'H', [0x24] = 'J',
        [0x25] = 'K', [0x26] = 'L', [0x27] = ':', [0x28] = '"',
        [0x29] = '~', [0x2B] = '|', [0x2C] = 'Z', [0x2D] = 'X',
        [0x2E] = 'C', [0x2F] = 'V', [0x30] = 'B', [0x31] = 'N',
        [0x32] = 'M', [0x33] = '<', [0x34] = '>', [0x35] = '?',
        [0x39] = ' '
    };

    if (code >= sizeof(normal)) {
        return 0;
    }
    char character = (left_shift || right_shift) && shifted[code]
        ? shifted[code]
        : normal[code];
    if ((left_control || right_control) &&
        ((character >= 'a' && character <= 'z') ||
         (character >= 'A' && character <= 'Z'))) {
        if (character >= 'A' && character <= 'Z') character += 'a' - 'A';
        int control_character = character - 'a' + 1;
        if (control_character == '\b') return '\b';
        if (control_character == '\t') return '\t';
        if (control_character == '\n' || control_character == '\r') {
            return '\n';
        }
        return TERMINAL_KEY_CTRL_BASE + control_character;
    }
    return character;
}

static int keyboard_getchar(void)
{
    if (!(inb(0x64) & 0x01)) {
        return -1;
    }

    uint8_t code = inb(0x60);
    if (code == 0xE0) {
        extended_scancode = 1;
        return -1;
    }
    if (extended_scancode) {
        extended_scancode = 0;
        if (code == 0x1D || code == 0x9D) {
            right_control = code == 0x1D;
            return -1;
        }
        if (code == 0x4B && (left_control || right_control)) {
            return TERMINAL_KEY_CTRL_LEFT;
        }
        if (code == 0x4D && (left_control || right_control)) {
            return TERMINAL_KEY_CTRL_RIGHT;
        }
        if (code == 0x48) return TERMINAL_KEY_UP;
        if (code == 0x50) return TERMINAL_KEY_DOWN;
        if (code == 0x4B) return TERMINAL_KEY_LEFT;
        if (code == 0x4D) return TERMINAL_KEY_RIGHT;
        if (code == 0x53) return TERMINAL_KEY_DELETE;
        if (code == 0x47) return TERMINAL_KEY_HOME;
        if (code == 0x4F) return TERMINAL_KEY_END;
        return -1;
    }

    if (code == 0x2A || code == 0x36) {
        if (code == 0x2A) left_shift = 1;
        else right_shift = 1;
        return -1;
    }
    if (code == 0xAA || code == 0xB6) {
        if (code == 0xAA) left_shift = 0;
        else right_shift = 0;
        return -1;
    }
    if (code == 0x1D || code == 0x9D) {
        left_control = code == 0x1D;
        return -1;
    }
    if (code & 0x80) {
        return -1;
    }

    return scancode_to_ascii(code);
}

lucid_status terminal_pre(void)
{
    return LUCID_OK_MSG("Preparing video and input consoles");
}

void terminal_set_boot_info(const multiboot_info *info)
{
    if (!info || !(info->flags & MULTIBOOT_INFO_FRAMEBUFFER)) {
        return;
    }

    if (info->framebuffer_type != MULTIBOOT_FRAMEBUFFER_RGB ||
        info->framebuffer_bpp != 32) {
        framebuffer_status =
            "GRUB framebuffer is not 32-bit RGB; using VGA output";
        return;
    }
    if (info->framebuffer_addr > 0xFFFFFFFFULL) {
        framebuffer_status =
            "GRUB framebuffer is above 4 GiB; using VGA output";
        return;
    }
    if (info->framebuffer_width < CURIE_WIDTH ||
        info->framebuffer_height < CURIE_HEIGHT ||
        info->framebuffer_width > 8192 ||
        info->framebuffer_height > 4320) {
        framebuffer_status =
            "GRUB framebuffer dimensions unsupported; using VGA output";
        return;
    }
    if (info->framebuffer_pitch < info->framebuffer_width * 4u ||
        (info->framebuffer_pitch & 3u) != 0) {
        framebuffer_status =
            "GRUB framebuffer pitch unsupported; using VGA output";
        return;
    }
    uint64_t length =
        (uint64_t)info->framebuffer_pitch * info->framebuffer_height;
    uint64_t end = info->framebuffer_addr + length;
    if (length == 0 || length > 64u * 1024u * 1024u ||
        end > 0xFFFFF000ULL) {
        framebuffer_status =
            "GRUB framebuffer memory range unsupported; using VGA output";
        return;
    }

    framebuffer_phys = (uint32_t)info->framebuffer_addr;
    framebuffer = (volatile uint8_t *)framebuffer_phys;
    framebuffer_length = (uint32_t)length;
    framebuffer_pitch = info->framebuffer_pitch;
    framebuffer_width = info->framebuffer_width;
    framebuffer_height = info->framebuffer_height;
    /* All color bits set produces white for a 32-bit RGB framebuffer. */
    framebuffer_foreground = 0xFFFFFFFFu;
    framebuffer_background = 0;
    framebuffer_status = "Curie framebuffer output ready";
}

int terminal_framebuffer_mapping(uint32_t *base, uint32_t *length)
{
    if (!framebuffer || !base || !length) {
        return 0;
    }
    *base = framebuffer_phys;
    *length = framebuffer_length;
    return 1;
}

lucid_status terminal_init(void)
{
    serial_init();
    left_shift = 0;
    right_shift = 0;
    left_control = 0;
    right_control = 0;
    extended_scancode = 0;
    serial_after_cr = 0;
    serial_escape_state = 0;
    serial_csi_parameters[0] = 0;
    serial_csi_parameters[1] = 0;
    serial_csi_parameter_count = 0;
    serial_csi_invalid = 0;
    serial_pending_valid = 0;
    if (framebuffer) {
        if (!serial_available) {
            return LUCID_OK_MSG(
                "Curie framebuffer output and PS/2 input ready"
            );
        }
        return LUCID_OK_MSG(
            "COM1, Curie framebuffer output, and serial/PS2 input ready"
        );
    }
    return LUCID_OK_MSG(framebuffer_status);
}

lucid_status terminal_post(void)
{
    return LUCID_OK;
}

static int serial_csi_key(int c)
{
    if (c >= '0' && c <= '9') {
        if (serial_csi_parameter_count == 0) {
            serial_csi_parameter_count = 1;
        }
        unsigned int index = serial_csi_parameter_count - 1;
        uint32_t digit = (uint32_t)(c - '0');
        if (index >= 2 ||
            serial_csi_parameters[index] > (999u - digit) / 10u) {
            serial_csi_invalid = 1;
        } else {
            serial_csi_parameters[index] =
                serial_csi_parameters[index] * 10u + digit;
        }
        return -1;
    }
    if (c == ';') {
        if (serial_csi_parameter_count == 0) {
            serial_csi_parameter_count = 1;
        }
        if (serial_csi_parameter_count >= 2) {
            serial_csi_invalid = 1;
        } else {
            serial_csi_parameter_count++;
        }
        return -1;
    }

    serial_escape_state = 0;
    if (serial_csi_invalid) return -1;
    if (c == 'A' && serial_csi_parameter_count <= 1) {
        return TERMINAL_KEY_UP;
    }
    if (c == 'B' && serial_csi_parameter_count <= 1) {
        return TERMINAL_KEY_DOWN;
    }
    if (c == 'C') {
        if (serial_csi_parameter_count == 2 &&
            serial_csi_parameters[0] == 1 &&
            serial_csi_parameters[1] == 5) {
            return TERMINAL_KEY_CTRL_RIGHT;
        }
        if (serial_csi_parameter_count <= 1) return TERMINAL_KEY_RIGHT;
    }
    if (c == 'D') {
        if (serial_csi_parameter_count == 2 &&
            serial_csi_parameters[0] == 1 &&
            serial_csi_parameters[1] == 5) {
            return TERMINAL_KEY_CTRL_LEFT;
        }
        if (serial_csi_parameter_count <= 1) return TERMINAL_KEY_LEFT;
    }
    if (c == 'H' && serial_csi_parameter_count <= 1) {
        return TERMINAL_KEY_HOME;
    }
    if (c == 'F' && serial_csi_parameter_count <= 1) {
        return TERMINAL_KEY_END;
    }
    if (c == '~' && serial_csi_parameter_count == 1) {
        if (serial_csi_parameters[0] == 1 ||
            serial_csi_parameters[0] == 7) return TERMINAL_KEY_HOME;
        if (serial_csi_parameters[0] == 3) return TERMINAL_KEY_DELETE;
        if (serial_csi_parameters[0] == 4 ||
            serial_csi_parameters[0] == 8) return TERMINAL_KEY_END;
    }
    return -1;
}

int terminal_getchar(void)
{
    for (;;) {
        int c;
        if (serial_pending_valid) {
            c = serial_pending_char;
            serial_pending_valid = 0;
        } else if (serial_available && (inb(COM1 + 5) & 0x01)) {
            c = inb(COM1);
        } else {
            int key = keyboard_getchar();
            if (key >= 0) return key;
            /* The syscall gate clears IF; sleep until the PIT wakes us. */
            __asm__ volatile ("sti\n\thlt\n\tcli" : : : "memory");
            continue;
        }

        if (serial_escape_state == 1) {
            if (c == '[') {
                serial_escape_state = 2;
                serial_csi_parameters[0] = 0;
                serial_csi_parameters[1] = 0;
                serial_csi_parameter_count = 0;
                serial_csi_invalid = 0;
                continue;
            }
            serial_escape_state = 0;
            serial_pending_char = (char)c;
            serial_pending_valid = 1;
            return '\x1B';
        }
        if (serial_escape_state == 2) {
            int key = serial_csi_key(c);
            if (key >= 0) return key;
            continue;
        }
        if (c == 0x1B) {
            serial_escape_state = 1;
            continue;
        }
        if (c == '\r') {
            serial_after_cr = 1;
            return '\n';
        }
        if (c == '\n' && serial_after_cr) {
            serial_after_cr = 0;
            continue;
        }
        serial_after_cr = 0;
        if (c >= 1 && c <= 26 &&
            c != '\b' && c != '\t' && c != '\n' && c != '\r') {
            return TERMINAL_KEY_CTRL_BASE + c;
        }
        return c == 0x7F ? '\b' : c;
    }
}
