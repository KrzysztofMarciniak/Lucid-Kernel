#include "impl/syscall/posix/posix_internal.h"
#include "recipes/ramfs.h"

static int32_t posix_seek_file(uint32_t fd,
                         int32_t offset,
                         uint32_t whence,
                         uint32_t slot)
{
    if (fd < POSIX_FIRST_FILE_FD) return syscall_posix_error(ESPIPE);
    posix_open_description *description =
        syscall_posix_get_description(slot, fd);
    if (!description) return syscall_posix_error(EBADF);
    if (whence > 2) return syscall_posix_error(EINVAL);
    if (syscall_file_lookup(description->path) != SYSCALL_FILE_OUTSIDE) {
        return syscall_posix_error(ESPIPE);
    }

    int64_t base = 0;
    if (whence == 1) {
        base = description->offset;
    } else if (whence == 2) {
        char *buffer = syscall_filesystem_buffer();
        size_t length = 0;
        int result = ramfs_read(
            description->path, buffer, SYS_FILE_MAX, &length
        );
        if (result != RAMFS_OK) return syscall_posix_ramfs_error(result);
        base = (int64_t)length;
    }
    int64_t next = base + offset;
    if (next < 0 || next > 0x7FFFFFFF) {
        return syscall_posix_error(EINVAL);
    }
    description->offset = (uint32_t)next;
    return (int32_t)next;
}

static uint32_t syscall_posix_lseek_handle(syscall_frame *frame)
{
    frame->eax = (uint32_t)posix_seek_file(
        frame->ebx, (int32_t)frame->ecx, frame->edx,
        syscall_posix_current_slot()
    );
    return 0;
}

SYSCALL_HANDLER(syscall_posix_lseek_entry,
                SYSCALL_POSIX_LSEEK,
                syscall_posix_lseek_handle);
