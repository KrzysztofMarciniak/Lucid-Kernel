#ifndef LUCID_IDT_DEBUG_H
#define LUCID_IDT_DEBUG_H

#include <stdint.h>

void idt_debug_format_vector(
    char *buf,
    uint32_t size,
    uint32_t vector
);

void idt_debug_format_page_fault(
    char *buf,
    uint32_t size,
    uint32_t vector,
    uint32_t error_code,
    uint32_t cr2
);

#endif
