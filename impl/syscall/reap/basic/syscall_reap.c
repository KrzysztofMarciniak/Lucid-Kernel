#include "impl/syscall/internal.h"

static uint32_t syscall_reap_handle(syscall_frame *frame)
{
    frame->eax = usermode_process_reap();
    return 0;
}

SYSCALL_HANDLER(syscall_reap_entry, SYS_REAP, syscall_reap_handle);
