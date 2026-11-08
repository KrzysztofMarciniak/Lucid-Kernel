#include "impl/syscall/posix/posix_internal.h"
#include "recipes/ramfs.h"

static int32_t posix_access_file(syscall_frame *frame)
{
    char path[SYS_PATH_MAX];
    int32_t result = syscall_posix_copy_user_path(frame->ebx, path);
    if (result < 0) return result;
    uint32_t mode = frame->ecx;
    if ((mode & ~7u) != 0) return syscall_posix_error(EINVAL);
    if (mode != 0) return syscall_posix_error(ENOTSUP);

    syscall_file_kind kind = syscall_file_lookup(path);
    if (kind == SYSCALL_FILE_UNKNOWN) {
        return syscall_posix_error(ENOENT);
    }
    if (kind == SYSCALL_FILE_OUTSIDE &&
        ramfs_is_directory(path) < 0) {
        return syscall_posix_error(ENOENT);
    }
    return 0;
}

static uint32_t syscall_posix_access_handle(syscall_frame *frame)
{
    frame->eax = (uint32_t)posix_access_file(frame);
    return 0;
}

SYSCALL_HANDLER(syscall_posix_access_entry,
                SYSCALL_POSIX_ACCESS,
                syscall_posix_access_handle);
