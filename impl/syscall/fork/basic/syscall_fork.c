#include "impl/syscall/internal.h"

static uint32_t syscall_fork_handle(syscall_frame *frame)
{
    frame->eax = usermode_process_fork(frame);
    return 0;
}

SYSCALL_HANDLER(syscall_fork_entry, SYS_FORK, syscall_fork_handle);
