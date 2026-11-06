#include <stdint.h>

uint32_t usermode_kernel_esp;

uint32_t usermode_return_esp(void)
{
    return usermode_kernel_esp;
}

void usermode_set_return_esp(uint32_t esp)
{
    usermode_kernel_esp = esp;
}
