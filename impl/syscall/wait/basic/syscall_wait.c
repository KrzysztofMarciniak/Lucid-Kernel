#include "impl/syscall/internal.h"

static uint32_t syscall_wait_handle(syscall_frame *frame)
{
    if (usermode_process_wait(frame->ebx, frame)) {
        return (uint32_t)usermode_process_schedule(frame);
    }
    return 0;
}

SYSCALL_HANDLER(syscall_wait_entry, SYS_WAIT, syscall_wait_handle);
