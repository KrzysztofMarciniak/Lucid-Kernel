#include "impl/syscall/posix/posix_internal.h"
#include "recipes/ramfs.h"

static int32_t posix_unlink_file(syscall_frame *frame)
{
    char path[SYS_PATH_MAX];
    int32_t result = syscall_posix_copy_user_path(frame->ebx, path);
    if (result < 0) return result;
    if (syscall_file_lookup(path) != SYSCALL_FILE_OUTSIDE) {
        return syscall_posix_error(EROFS);
    }
    result = ramfs_unlink(path);
    return result == RAMFS_OK ? 0 : syscall_posix_ramfs_error(result);
}

uint32_t syscall_posix_unlink_handle(syscall_frame *frame)
{
    frame->eax = (uint32_t)posix_unlink_file(frame);
    return 0;
}
