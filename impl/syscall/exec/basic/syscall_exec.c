#include "impl/syscall/internal.h"

static uint32_t syscall_exec_handle(syscall_frame *frame)
{
    char path[SYS_PATH_MAX];
    if (frame->ecx == 0 || frame->ecx >= sizeof(path) ||
        !syscall_user_range_valid(frame->ebx, frame->ecx, 0)) {
        frame->eax = SYS_UNKNOWN_TEST;
        return 0;
    }

    const char *user_path = (const char *)frame->ebx;
    for (uint32_t i = 0; i < frame->ecx; i++) path[i] = user_path[i];
    path[frame->ecx] = '\0';
    int result = usermode_process_exec(path, frame);
    if (result < 0) {
        return (uint32_t)usermode_process_schedule(frame);
    }
    frame->eax = result ? 0 : SYS_UNKNOWN_TEST;
    return 0;
}

SYSCALL_HANDLER(syscall_exec_entry, SYS_EXEC, syscall_exec_handle);
