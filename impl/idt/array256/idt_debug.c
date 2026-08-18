#include "idt_debug.h"

static void append_char(char *buf, uint32_t size, uint32_t *pos, char c)
{
    if (*pos < size - 1) {
        buf[*pos] = c;
        (*pos)++;
    }
}

static void append_string(char *buf,
                          uint32_t size,
                          uint32_t *pos,
                          const char *str)
{
    while (*str) {
        append_char(buf, size, pos, *str++);
    }
}

static void append_u32(char *buf,
                       uint32_t size,
                       uint32_t *pos,
                       uint32_t value)
{
    char tmp[10];
    uint32_t count = 0;

    if (value == 0) {
        append_char(buf, size, pos, '0');
        return;
    }

    while (value != 0) {
        tmp[count++] = '0' + (value % 10);
        value /= 10;
    }

    while (count > 0) {
        append_char(buf, size, pos, tmp[--count]);
    }
}

static void append_hex(char *buf,
                       uint32_t size,
                       uint32_t *pos,
                       uint32_t value)
{
    static const char hex[] = "0123456789ABCDEF";

    append_string(buf, size, pos, "0x");

    for (int i = 7; i >= 0; i--) {
        append_char(
            buf,
            size,
            pos,
            hex[(value >> (i * 4)) & 0xF]
        );
    }
}

void idt_debug_format_vector(char *buf,
                             uint32_t size,
                             uint32_t vector)
{
    uint32_t pos = 0;

    append_string(
        buf,
        size,
        &pos,
        "unexpected interrupt vector "
    );

    append_u32(
        buf,
        size,
        &pos,
        vector
    );

    buf[pos] = 0;
}


void idt_debug_format_page_fault(char *buf,
                                 uint32_t size,
                                 uint32_t vector,
                                 uint32_t error_code,
                                 uint32_t cr2)
{
    uint32_t pos = 0;

    append_string(
        buf,
        size,
        &pos,
        "page fault vector "
    );

    append_u32(
        buf,
        size,
        &pos,
        vector
    );

    append_string(
        buf,
        size,
        &pos,
        " error="
    );

    append_hex(
        buf,
        size,
        &pos,
        error_code
    );

    append_string(
        buf,
        size,
        &pos,
        " CR2="
    );

    append_hex(
        buf,
        size,
        &pos,
        cr2
    );

    buf[pos] = 0;
}
