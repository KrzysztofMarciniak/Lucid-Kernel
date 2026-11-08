#include "impl/syscall/internal.h"
#include "recipes/acpi.h"
#include "recipes/terminal.h"

void syscall_poweroff_system(void)
{
    terminal_write("Powering off...\n");
    terminal_flush();
    __asm__ volatile ("cli");
    acpi_poweroff();
    terminal_write("Poweroff request returned; CPU halted.\n");
    terminal_flush();
    for (;;) __asm__ volatile ("hlt");
}
