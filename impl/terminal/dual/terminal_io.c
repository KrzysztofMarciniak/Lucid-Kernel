#include "terminal_io.h"
#include "recipes/terminal.h"
#include "stdint.h"

#define COM1 0x3F8

static int left_shift;
static int right_shift;
static int left_control;
static int right_control;
static int left_alt;
static int right_alt;
static int extended_scancode;
static int serial_after_cr;
static int serial_available;
static int serial_initialized;
static int serial_escape_state;
static uint32_t serial_csi_parameters[2];
static unsigned int serial_csi_parameter_count;
static int serial_csi_invalid;

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

static void serial_putc(char c)
{
    if (!serial_available) return;
    while (!(inb(COM1 + 5) & 0x20)) {
    }
    outb(COM1, (uint8_t)c);
}

void terminal_io_init(void)
{
    serial_init();
    left_shift = 0;
    right_shift = 0;
    left_control = 0;
    right_control = 0;
    left_alt = 0;
    right_alt = 0;
    extended_scancode = 0;
    serial_after_cr = 0;
    serial_escape_state = 0;
    serial_csi_parameters[0] = 0;
    serial_csi_parameters[1] = 0;
    serial_csi_parameter_count = 0;
    serial_csi_invalid = 0;
}

int terminal_io_available(void)
{
    serial_init();
    return serial_available;
}

void terminal_io_putc(char c)
{
    serial_init();
    serial_putc(c);
}

void terminal_io_clear(void)
{
    serial_init();
    if (!serial_available) return;
    serial_putc('\x1B');
    serial_putc('[');
    serial_putc('2');
    serial_putc('J');
    serial_putc('\x1B');
    serial_putc('[');
    serial_putc('H');
}

void terminal_io_flush(void)
{
    serial_init();
    if (!serial_available) return;
    for (uint32_t spin = 0; spin < 1000000u; spin++) {
        if (inb(COM1 + 5) & 0x40) return;
    }
}

static int scancode_to_ascii(uint8_t code)
{
    static const char normal[0x40] = {
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

    if (code >= sizeof(normal)) return 0;
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
    if (!(inb(0x64) & 0x01)) return -1;

    uint8_t code = inb(0x60);
    if (code == 0xE0) {
        extended_scancode = 1;
        return -1;
    }
    if (extended_scancode) {
        extended_scancode = 0;
        if (code == 0x38 || code == 0xB8) {
            right_alt = code == 0x38;
            return -1;
        }
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
    if (code == 0x38 || code == 0xB8) {
        left_alt = code == 0x38;
        return -1;
    }
    if (code & 0x80) return -1;

    int character = scancode_to_ascii(code);
    if (left_alt || right_alt) {
        int shifted = left_shift || right_shift;
        if (shifted && code >= 0x02 && code <= 0x0B) {
            static const char digits[] = "1234567890";
            character = digits[code - 0x02];
        }
        return (shifted ? TERMINAL_KEY_MOD_SHIFT_BASE
                        : TERMINAL_KEY_MOD_BASE) + character;
    }
    return character;
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

int terminal_input_poll_key(void)
{
    serial_init();
    for (;;) {
        int c;
        if (serial_available && (inb(COM1 + 5) & 0x01)) {
            c = inb(COM1);
        } else {
            return keyboard_getchar();
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
            if (c == '\r' || c == '\n') {
                return TERMINAL_KEY_MOD_BASE + '\n';
            }
            if (c >= 'A' && c <= 'Z') {
                return TERMINAL_KEY_MOD_SHIFT_BASE + c;
            }
            int shifted_digit = 1;
            if (c == '!') c = '1';
            else if (c == '@') c = '2';
            else if (c == '#') c = '3';
            else if (c == '$') c = '4';
            else if (c == '%') c = '5';
            else if (c == '^') c = '6';
            else if (c == '&') c = '7';
            else if (c == '*') c = '8';
            else if (c == '(') c = '9';
            else if (c == ')') c = '0';
            else shifted_digit = 0;
            return (shifted_digit
                ? TERMINAL_KEY_MOD_SHIFT_BASE
                : TERMINAL_KEY_MOD_BASE) + c;
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
