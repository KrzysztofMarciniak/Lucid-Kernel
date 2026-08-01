#include "recipes/idt.h"
#include "recipes/status.h"
#include "internal.h"

struct idt_entry idt[IDT_ENTRIES];
struct idt_ptr   idtp;

lucid_status idt_pre(void) {
    return LUCID_OK;
}

lucid_status idt_init(void) {
    for (int i = 0; i < IDT_ENTRIES; i++) {
        idt[i].offset_low = idt[i].offset_high = 0;
        idt[i].selector = 0;
        idt[i].zero = 0;
        idt[i].type_attr = 0;
    }

    isr_stub_table_install();

    idtp.limit = sizeof(idt) - 1;
    idtp.base  = (uint32_t)&idt;

    return LUCID_OK_MSG("IDT built (256 entries)");
}

lucid_status idt_post(void) {
    idt_lidt(&idtp);
    return LUCID_OK_MSG("IDT loaded (lidt)");
}

void isr_common_handler(uint32_t int_no) {
    static char debug_buf[32] = "vector: ";
    char numbuf[4];
    int len = 0;
    uint32_t n = int_no;
    if (n == 0) { numbuf[len++] = '0'; }
    while (n > 0 && len < 3) { numbuf[len++] = '0' + (n % 10); n /= 10; }
    int base = 8; /* length of "vector: " */
    for (int i = 0; i < len; i++) debug_buf[base + i] = numbuf[len - 1 - i];
    debug_buf[base + len] = '\0';

    LUCID_FAIL_EX(
        1,
        "Unhandled interrupt",
        debug_buf,
        "add a real handler once a recipe needs this interrupt"
    );
}

void idt_install_handler(int vector, void (*handler)(struct interrupt_frame *)) {
    idt_set_gate(vector, (uint32_t)handler);
}
