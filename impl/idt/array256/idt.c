#include "recipes/idt.h"
#include "recipes/status.h"
#include "idt_debug.h"
#include "internal.h"
#include "panic.h"

struct idt_entry idt[IDT_ENTRIES];
struct idt_ptr idtp;

lucid_status idt_pre(void) {
    return LUCID_OK;
}

lucid_status idt_init(void) {
    for (int i = 0; i < IDT_ENTRIES; i++) {
        idt[i].offset_low = 0;
        idt[i].offset_high = 0;
        idt[i].selector = 0;
        idt[i].zero = 0;
        idt[i].type_attr = 0;
    }

    isr_stub_table_install();

    idtp.limit = sizeof(idt) - 1;
    idtp.base = (uint32_t)&idt;

    return LUCID_OK_MSG("IDT built (256 entries)");
}

lucid_status idt_post(void) {
    idt_lidt(&idtp);
    return LUCID_OK_MSG("IDT loaded (lidt)");
}
void isr_common_handler(uint32_t int_no, uint32_t error_code)
{
    static char debug[96];

    if (int_no == 14) {
        uint32_t cr2;

        __asm__ volatile (
            "mov %%cr2, %0"
            : "=r"(cr2)
        );

        idt_debug_format_page_fault(
            debug,
            sizeof(debug),
            int_no,
            error_code,
            cr2
        );

        panic_status(LUCID_FAIL_EX(
            int_no,
            "Page fault",
            debug,
            "check user mapping and page permissions"
        ));
    }

    idt_debug_format_vector(
        debug,
        sizeof(debug),
        int_no
    );

    panic_status(LUCID_FAIL_EX(
        int_no,
        "Unhandled interrupt",
        debug,
        "add a handler for this vector"
    ));
}

void idt_install_handler(int vector, uint32_t handler_addr) {
    idt_set_gate(vector, handler_addr);
}

