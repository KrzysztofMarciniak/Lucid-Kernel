#include "impl/syscall/internal.h"
#include "recipes/terminal.h"

static uint32_t syscall_terminal_size_handle(syscall_frame *frame)
{
    frame->eax = terminal_get_size();
    return 0;
}

SYSCALL_HANDLER(
    syscall_terminal_size_entry, SYS_GET_TERMINAL_SIZE,
    syscall_terminal_size_handle
);
