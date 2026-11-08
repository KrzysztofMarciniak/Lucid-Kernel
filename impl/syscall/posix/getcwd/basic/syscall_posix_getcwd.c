#include "impl/syscall/posix/posix_internal.h"

static int32_t posix_getcwd_path(syscall_frame *frame)
{
    if (frame->ebx == 0) return syscall_posix_error(EINVAL);
    if (frame->ecx < 2) return syscall_posix_error(ERANGE);
    if (!syscall_user_range_valid(frame->ebx, 2, 1)) {
        return syscall_posix_error(EFAULT);
    }
    char *buffer = (char *)frame->ebx;
    buffer[0] = '/';
    buffer[1] = '\0';
    return 0;
}

static uint32_t syscall_posix_getcwd_handle(syscall_frame *frame)
{
    frame->eax = (uint32_t)posix_getcwd_path(frame);
    return 0;
}

SYSCALL_HANDLER(syscall_posix_getcwd_entry,
                SYSCALL_POSIX_GETCWD,
                syscall_posix_getcwd_handle);
