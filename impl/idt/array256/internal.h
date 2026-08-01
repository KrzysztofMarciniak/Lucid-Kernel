#ifndef LUCID_IMPL_IDT_ARRAY256_INTERNAL_H
#define LUCID_IMPL_IDT_ARRAY256_INTERNAL_H

#include <stdint.h>

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  zero;
    uint8_t  type_attr;
    uint16_t offset_high;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

#define IDT_ENTRIES 256
#define KERNEL_CODE_SELECTOR 0x08

extern struct idt_entry idt[IDT_ENTRIES];
extern struct idt_ptr   idtp;

void idt_lidt(struct idt_ptr *p);
void isr_stub_table_install(void);
void isr_common_handler(uint32_t int_no);

static inline void idt_lidt(struct idt_ptr *p) {
    __asm__ volatile ("lidt (%0)" : : "r"(p));
}

#endif
