#include "internal.h"

void usermode_drop(uint32_t entry, uint32_t user_stack, uint32_t code_sel, uint32_t data_sel) {
    __asm__ volatile (
        "mov %1, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"

        "push %1\n"      /* ss */
        "push %2\n"      /* esp */
        "pushf\n"        /* eflags */
        "push %0\n"      /* cs */
        "push %3\n"      /* eip */
        "iret\n"
        :
        : "r"(code_sel), "r"(data_sel), "r"(user_stack), "r"(entry)
        : "eax", "memory"
    );
}
