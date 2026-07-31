/* kernel.c - fixed core. Never swapped, no config.mk knob.
 *
 * The only piece of code allowed to know about every recipe by name,
 * since it's what drives the three-pass boot: all _pre, then all
 * _init, then all _post, in INIT_BOOT order within each pass.
 */
#include "init_table.h"
#include "panic.h"

void kernel_main(void) {
    unsigned int i;

    for (i = 0; i < init_table_count; i++) {
        lucid_status st = init_table[i].pre();
        if (st.ok) panic(st.msg);
    }

    for (i = 0; i < init_table_count; i++) {
        lucid_status st = init_table[i].init();
        if (st.ok) panic(st.msg);
    }

    for (i = 0; i < init_table_count; i++) {
        lucid_status st = init_table[i].post();
        if (st.ok) panic(st.msg);
    }

    panic_ok("lucid: boot complete, all recipes up, halting.");

    for (;;) {
        __asm__ volatile ("hlt");
    }
}
