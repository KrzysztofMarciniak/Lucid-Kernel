#include "internal.h"

void idt_set_gate(int n, uint32_t handler_addr) {
    idt[n].offset_low  = handler_addr & 0xFFFF;
    idt[n].offset_high = (handler_addr >> 16) & 0xFFFF;
    idt[n].selector    = KERNEL_CODE_SELECTOR;
    idt[n].zero        = 0;
    idt[n].type_attr   = 0x8E; /* present, ring0, 32-bit interrupt gate */
}

void isr_stub_table_install(void) {
    for (int i = 0; i < IDT_ENTRIES; i++) {
        idt_set_gate(i, isr_stub_table[i]);
    }

    /*
     * Allow ring3 to execute:
     *
     *     int3
     *
     * 0xEE:
     *   Present = 1
     *   DPL     = 3
     *   Type    = 32-bit interrupt gate
     */
    idt[3].type_attr = 0xEE;
}
