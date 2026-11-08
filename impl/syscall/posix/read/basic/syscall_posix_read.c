#include "impl/syscall/posix/posix_internal.h"
#include "recipes/ramfs.h"

static int32_t posix_read_file(syscall_frame *frame, uint32_t slot)
{
    uint32_t fd = frame->ebx;
    uint32_t count = frame->edx;
    posix_process_state *state = syscall_posix_state(slot);

    if (fd < POSIX_FIRST_FILE_FD) {
        if (fd >= 3 || (state->closed_standard & (1u << fd)) != 0) {
            return syscall_posix_error(EBADF);
        }
        return syscall_posix_error(fd == 0 ? ENOTSUP : EBADF);
    }

    posix_open_description *description =
        syscall_posix_get_description(slot, fd);
    if (!description) return syscall_posix_error(EBADF);
    if ((description->flags & LUCID_POSIX_O_ACCMODE) ==
        LUCID_POSIX_O_WRONLY) {
        return syscall_posix_error(EBADF);
    }
    if (count == 0) return 0;
    if (count > SYS_FILE_MAX) count = SYS_FILE_MAX;
    if (!syscall_user_range_valid(frame->ecx, count, 1)) {
        return syscall_posix_error(EFAULT);
    }

    char *buffer = syscall_filesystem_buffer();
    size_t length = 0;
    int result = syscall_file_read(
        description->path, buffer, SYS_FILE_MAX, &length, frame
    );
    if (result == 0) {
        result = ramfs_read(
            description->path, buffer, SYS_FILE_MAX, &length
        );
        if (result != RAMFS_OK) return syscall_posix_ramfs_error(result);
    } else if (result < 0) {
        return syscall_posix_error(EIO);
    }

    if (description->offset >= length) return 0;
    size_t amount = length - description->offset;
    if (amount > count) amount = count;
    char *user_buffer = (char *)frame->ecx;
    for (size_t i = 0; i < amount; i++) {
        user_buffer[i] = buffer[description->offset + i];
    }
    description->offset += (uint32_t)amount;
    return (int32_t)amount;
}

uint32_t syscall_posix_read_handle(syscall_frame *frame)
{
    frame->eax = (uint32_t)posix_read_file(
        frame, syscall_posix_current_slot()
    );
    return 0;
}
