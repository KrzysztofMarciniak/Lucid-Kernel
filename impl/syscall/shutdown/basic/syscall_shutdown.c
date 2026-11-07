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

static uint32_t syscall_shutdown_handle(syscall_frame *frame)
{
    (void)frame;
    syscall_poweroff_system();
    return 0;
}

SYSCALL_HANDLER(
    syscall_shutdown_entry, SYS_SHUTDOWN, syscall_shutdown_handle
);
