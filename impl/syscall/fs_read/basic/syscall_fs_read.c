#include "impl/syscall/internal.h"
#include "recipes/ramfs.h"
#include "recipes/usermode.h"

static uint32_t syscall_fs_read_handle(syscall_frame *frame)
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
    if (syscall_text_equals(path, "/system/terminals/readline")) {
        return syscall_readline_file_handle(frame);
    }

    int result = syscall_file_read(
        path, buffer, frame->esi, &length, frame
    );
    if (result == 3) {
        return (uint32_t)usermode_process_schedule(frame);
    }
    if (result == 2) return 0;
    if (result < 0 ||
        (result == 0 &&
         ramfs_read(path, buffer, frame->esi, &length))) {
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

SYSCALL_HANDLER(syscall_fs_read_entry, SYS_FS_READ, syscall_fs_read_handle);
