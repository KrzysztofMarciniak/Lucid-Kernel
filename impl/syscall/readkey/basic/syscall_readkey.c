#include "impl/syscall/internal.h"
#include "recipes/terminal.h"

static uint32_t syscall_readkey_handle(syscall_frame *frame)
{
    frame->eax = (uint32_t)terminal_getchar();
    return 0;
}

SYSCALL_HANDLER(syscall_readkey_entry, SYS_READKEY, syscall_readkey_handle);
