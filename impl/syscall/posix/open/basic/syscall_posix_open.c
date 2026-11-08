#include "impl/syscall/posix/posix_internal.h"
#include "recipes/ramfs.h"

static int32_t posix_open_file(syscall_frame *frame, uint32_t slot)
{
    char path[SYS_PATH_MAX];
    int32_t result = syscall_posix_copy_user_path(frame->ebx, path);
    if (result < 0) return result;

    uint32_t flags = frame->ecx;
    uint32_t access_mode = flags & LUCID_POSIX_O_ACCMODE;
    const uint32_t allowed_flags = LUCID_POSIX_O_ACCMODE |
        LUCID_POSIX_O_CREAT | LUCID_POSIX_O_EXCL |
        LUCID_POSIX_O_TRUNC | LUCID_POSIX_O_APPEND;
    if ((flags & ~allowed_flags) != 0 ||
        access_mode == LUCID_POSIX_O_ACCMODE ||
        ((flags & LUCID_POSIX_O_TRUNC) != 0 &&
         access_mode == LUCID_POSIX_O_RDONLY)) {
        return syscall_posix_error(EINVAL);
    }

    int fd = syscall_posix_find_free_descriptor(slot);
    int description_index = syscall_posix_find_free_description();
    if (fd < 0 || description_index < 0) {
        return syscall_posix_error(EMFILE);
    }

    syscall_file_kind kind = syscall_file_lookup(path);
    if (kind == SYSCALL_FILE_DIRECTORY) return syscall_posix_error(EISDIR);
    if (kind == SYSCALL_FILE_UNKNOWN) return syscall_posix_error(ENOENT);
    int is_virtual_file = kind != SYSCALL_FILE_OUTSIDE;
    if (is_virtual_file && access_mode != LUCID_POSIX_O_RDONLY) {
        return syscall_posix_error(EROFS);
    }

    int directory = is_virtual_file ? 0 : ramfs_is_directory(path);
    if (directory == 1) return syscall_posix_error(EISDIR);
    int exists = directory == 0 || is_virtual_file;
    if (exists && (flags & (LUCID_POSIX_O_CREAT | LUCID_POSIX_O_EXCL)) ==
            (LUCID_POSIX_O_CREAT | LUCID_POSIX_O_EXCL)) {
        return syscall_posix_error(EEXIST);
    }

    if (!exists) {
        if ((flags & LUCID_POSIX_O_CREAT) == 0) {
            return syscall_posix_error(ENOENT);
        }
        result = ramfs_write(path, "", 0);
        if (result != RAMFS_OK) return syscall_posix_ramfs_error(result);
    }

    if ((flags & LUCID_POSIX_O_TRUNC) != 0) {
        result = ramfs_write(path, "", 0);
        if (result != RAMFS_OK) return syscall_posix_ramfs_error(result);
    }

    posix_open_description *description =
        syscall_posix_description((uint32_t)description_index);
    description->used = 1;
    description->references = 1;
    description->flags = (uint16_t)flags;
    description->offset = 0;
    for (uint32_t i = 0; i < SYS_PATH_MAX; i++) {
        description->path[i] = path[i];
        if (path[i] == '\0') break;
    }
    if ((flags & LUCID_POSIX_O_APPEND) != 0) {
        char *buffer = syscall_filesystem_buffer();
        size_t length = 0;
        int read_result = syscall_file_read(
            path, buffer, SYS_FILE_MAX, &length, 0
        );
        if (read_result == 0) {
            read_result = ramfs_read(
                path, buffer, SYS_FILE_MAX, &length
            );
            if (read_result != RAMFS_OK) {
                syscall_posix_release_description(
                    (uint32_t)description_index
                );
                return syscall_posix_ramfs_error(read_result);
            }
        } else if (read_result < 0) {
            syscall_posix_release_description((uint32_t)description_index);
            return syscall_posix_error(EIO);
        }
        description->offset = (uint32_t)length;
    }

    syscall_posix_install_description(
        slot, (uint32_t)fd, (uint32_t)description_index
    );
    return fd;
}

uint32_t syscall_posix_open_handle(syscall_frame *frame)
{
    frame->eax = (uint32_t)posix_open_file(
        frame, syscall_posix_current_slot()
    );
    return 0;
}
