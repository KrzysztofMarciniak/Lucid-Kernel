#include "impl/syscall/internal.h"

static uint32_t syscall_exit_handle(syscall_frame *frame)
{
    usermode_process_exit(frame->ebx);
    return (uint32_t)usermode_process_schedule(frame);
}

SYSCALL_HANDLER(syscall_exit_entry, SYS_EXIT, syscall_exit_handle);
