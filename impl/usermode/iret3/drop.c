#include <stdint.h>
void usermode_drop(
    uint32_t entry,
    uint32_t stack,
    uint32_t cs,
    uint32_t ds
)
{
    __asm__ volatile(
        "cli\n"

        "mov %[ds], %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"

        "push %[ds]\n"
        "push %[stack]\n"
        "push $0x202\n"
        "push %[cs]\n"
        "push %[entry]\n"

        "iret\n"

        :
        :
        [entry] "a"(entry),
        [stack] "b"(stack),
        [cs] "c"(cs),
        [ds] "d"(ds)
        :
        "memory"
    );
}
