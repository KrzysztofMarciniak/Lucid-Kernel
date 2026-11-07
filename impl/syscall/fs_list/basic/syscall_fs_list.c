#include "impl/syscall/internal.h"
#include "recipes/ramfs.h"

static uint32_t syscall_fs_list_handle(syscall_frame *frame)
{
    char path[SYS_PATH_MAX];
    char *buffer = syscall_filesystem_buffer();
    size_t length = 0;
    if (frame->ecx == 0 || frame->ecx >= sizeof(path) ||
        frame->esi > SYS_FILE_MAX ||
        !syscall_user_range_valid(frame->ebx, frame->ecx, 0) ||
        !syscall_user_range_valid(frame->edx, frame->esi, 1)) {
        frame->eax = SYS_UNKNOWN_TEST;
        return 0;
    }

    const char *user_path = (const char *)frame->ebx;
    for (uint32_t i = 0; i < frame->ecx; i++) path[i] = user_path[i];
    path[frame->ecx] = '\0';
    int result = syscall_file_list(
        path, buffer, frame->esi, &length
    );
    if (result < 0 ||
        (result == 0 &&
         ramfs_list(path, buffer, frame->esi, &length))) {
        frame->eax = SYS_UNKNOWN_TEST;
        return 0;
    }

    char *user_buffer = (char *)frame->edx;
    for (size_t i = 0; i < length; i++) {
        user_buffer[i] = buffer[i];
    }
    frame->eax = (uint32_t)length;
    return 0;
}

SYSCALL_HANDLER(syscall_fs_list_entry, SYS_FS_LIST, syscall_fs_list_handle);
