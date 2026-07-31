#ifndef GDT_FLAT_INTERNAL_H
#define GDT_FLAT_INTERNAL_H

#include "stdint.h"

/* Private to impl/gdt/flat/. Nothing outside this directory may
 * include this header -- the recipe (recipes/gdt.h) is the only
 * public surface of this implementation. */

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

/* Shared between gdt.c and tss.c within this impl only. */
void gdt_set_gate(int num, uint32_t base, uint32_t limit,
                   uint8_t access, uint8_t gran_flags);

void tss_install(int gdt_index, uint16_t kernel_ss, uint32_t kernel_esp0);
void tss_load(void);
void tss_set_kernel_stack(uint32_t esp0);

#endif
