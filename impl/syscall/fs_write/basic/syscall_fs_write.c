#include "impl/syscall/internal.h"
#include "recipes/ramfs.h"
#include "recipes/terminal.h"

static uint32_t syscall_fs_write_handle(syscall_frame *frame)
{
    char path[SYS_PATH_MAX];
    char *buffer = syscall_filesystem_buffer();
    if (frame->ecx == 0 || frame->ecx >= sizeof(path) ||
        frame->esi > SYS_FILE_MAX ||
        !syscall_user_range_valid(frame->ebx, frame->ecx, 0) ||
        !syscall_user_range_valid(frame->edx, frame->esi, 0)) {
        frame->eax = SYS_UNKNOWN_TEST;
        return 0;
    }

    const char *user_path = (const char *)frame->ebx;
    const char *user_data = (const char *)frame->edx;
    for (uint32_t i = 0; i < frame->ecx; i++) path[i] = user_path[i];
    path[frame->ecx] = '\0';
    for (uint32_t i = 0; i < frame->esi; i++) {
        buffer[i] = user_data[i];
    }
    int result = syscall_file_write(path, buffer, frame->esi);
    if (result < 0 ||
        (result == 0 && ramfs_write(path, buffer, frame->esi))) {
        frame->eax = SYS_UNKNOWN_TEST;
        return 0;
    }
    if (syscall_text_equals(path, "/system/config/clearsh/zoom") ||
        syscall_text_equals(path, "/system/config/brain.conf")) {
        terminal_config_reload();
    }
    frame->eax = frame->esi;
    return 0;
}

SYSCALL_HANDLER(syscall_fs_write_entry, SYS_FS_WRITE, syscall_fs_write_handle);
