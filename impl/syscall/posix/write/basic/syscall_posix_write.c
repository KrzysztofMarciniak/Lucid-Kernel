#include "impl/syscall/posix/posix_internal.h"
#include "recipes/ramfs.h"
#include "recipes/terminal.h"

static char write_buffers[USERMODE_PROCESS_SLOTS][SYS_FILE_MAX];

static int32_t posix_write_file(syscall_frame *frame, uint32_t slot)
{
    uint32_t fd = frame->ebx;
    uint32_t count = frame->edx;
    posix_process_state *state = syscall_posix_state(slot);

    if (fd < POSIX_FIRST_FILE_FD) {
        if (fd >= 3 || (state->closed_standard & (1u << fd)) != 0) {
            return syscall_posix_error(EBADF);
        }
        if (fd == 0) return syscall_posix_error(EBADF);
        if (count > SYS_WRITE_MAX) count = SYS_WRITE_MAX;
        if (count == 0) return 0;
        if (!syscall_user_range_valid(frame->ecx, count, 0)) {
            return syscall_posix_error(EFAULT);
        }
        const char *text = (const char *)frame->ecx;
        for (uint32_t i = 0; i < count; i++) terminal_putc(text[i]);
        return (int32_t)count;
    }

    posix_open_description *description =
        syscall_posix_get_description(slot, fd);
    if (!description) return syscall_posix_error(EBADF);
    if ((description->flags & LUCID_POSIX_O_ACCMODE) ==
        LUCID_POSIX_O_RDONLY) {
        return syscall_posix_error(EBADF);
    }
    if (count == 0) return 0;
    if (count > SYS_FILE_MAX) count = SYS_FILE_MAX;
    if (!syscall_user_range_valid(frame->ecx, count, 0)) {
        return syscall_posix_error(EFAULT);
    }
    if ((description->flags & LUCID_POSIX_O_APPEND) != 0) {
        char *read_buffer = syscall_filesystem_buffer();
        size_t length = 0;
        int read_result = ramfs_read(
            description->path, read_buffer, SYS_FILE_MAX, &length
        );
        if (read_result != RAMFS_OK) {
            return syscall_posix_ramfs_error(read_result);
        }
        description->offset = (uint32_t)length;
    }
    if (description->offset >= SYS_FILE_MAX) {
        return syscall_posix_error(ENOSPC);
    }
    uint32_t available = SYS_FILE_MAX - description->offset;
    if (count > available) count = available;

    char *input = write_buffers[slot];
    const char *user_buffer = (const char *)frame->ecx;
    for (uint32_t i = 0; i < count; i++) input[i] = user_buffer[i];

    char *contents = syscall_filesystem_buffer();
    size_t length = 0;
    int result = ramfs_read(
        description->path, contents, SYS_FILE_MAX, &length
    );
    if (result != RAMFS_OK) return syscall_posix_ramfs_error(result);

    for (uint32_t i = (uint32_t)length; i < description->offset; i++) {
        contents[i] = '\0';
    }
    uint32_t end = description->offset + count;
    for (uint32_t i = 0; i < count; i++) {
        contents[description->offset + i] = input[i];
    }
    if (end > length) length = end;

    result = ramfs_write(description->path, contents, length);
    if (result != RAMFS_OK) return syscall_posix_ramfs_error(result);
    description->offset = end;
    return (int32_t)count;
}

static uint32_t syscall_posix_write_handle(syscall_frame *frame)
{
    frame->eax = (uint32_t)posix_write_file(
        frame, syscall_posix_current_slot()
    );
    return 0;
}

SYSCALL_HANDLER(syscall_posix_write_entry,
                SYSCALL_POSIX_WRITE,
                syscall_posix_write_handle);
