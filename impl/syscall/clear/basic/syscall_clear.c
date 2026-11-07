#include "impl/syscall/internal.h"
#include "recipes/terminal.h"

static uint32_t syscall_clear_handle(syscall_frame *frame)
{
    terminal_clear();
    frame->eax = 0;
    return 0;
}

SYSCALL_HANDLER(syscall_clear_entry, SYS_CLEAR, syscall_clear_handle);
