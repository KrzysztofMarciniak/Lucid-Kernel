#include "impl/syscall/internal.h"
#include "recipes/ramfs.h"

static uint32_t syscall_fs_stat_handle(syscall_frame *frame)
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

    syscall_file_kind kind = syscall_file_lookup(path);
    if (kind == SYSCALL_FILE_DIRECTORY) {
        frame->eax = 1;
        return 0;
    }
    if (kind != SYSCALL_FILE_OUTSIDE) {
        frame->eax = kind == SYSCALL_FILE_UNKNOWN
            ? SYS_UNKNOWN_TEST
            : 0;
        return 0;
    }

    int is_directory = ramfs_is_directory(path);
    frame->eax = is_directory < 0
        ? SYS_UNKNOWN_TEST
        : (uint32_t)is_directory;
    return 0;
}

SYSCALL_HANDLER(syscall_fs_stat_entry, SYS_FS_STAT, syscall_fs_stat_handle);
