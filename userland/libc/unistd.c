#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>

#include "recipes/syscall_abi.h"
#include "recipes/syscall_posix.h"

int errno;

static int normalize_path(const char *path,
                          char normalized[SYS_PATH_MAX],
                          uint32_t *normalized_length)
{
    if (!path) {
        errno = EINVAL;
        return 0;
    }

    uint32_t length = 0;
    while (path[length]) {
        if (length + 1 >= SYS_PATH_MAX) {
            errno = ENAMETOOLONG;
            return 0;
        }
        length++;
    }
    if (length == 0) {
        errno = ENOENT;
        return 0;
    }

    uint32_t used = 1;
    normalized[0] = '/';
    uint32_t cursor = 0;
    while (cursor < length) {
        while (cursor < length && path[cursor] == '/') cursor++;
        if (cursor == length) break;

        uint32_t component_start = cursor;
        while (cursor < length && path[cursor] != '/') cursor++;
        uint32_t component_length = cursor - component_start;
        if (component_length == 1 && path[component_start] == '.') {
            continue;
        }
        if (component_length == 2 &&
            path[component_start] == '.' &&
            path[component_start + 1] == '.') {
            if (used > 1) {
                if (normalized[used - 1] == '/') used--;
                while (used > 1 && normalized[used - 1] != '/') used--;
                if (used > 1) used--;
            }
            continue;
        }

        uint32_t separator =
            used > 1 && normalized[used - 1] != '/' ? 1u : 0u;
        if (used + separator + component_length >= SYS_PATH_MAX) {
            errno = ENAMETOOLONG;
            return 0;
        }
        if (separator) normalized[used++] = '/';
        for (uint32_t i = 0; i < component_length; i++) {
            normalized[used++] = path[component_start + i];
        }
    }

    normalized[used] = '\0';
    *normalized_length = used;
    return 1;
}

static int posix_result(uint32_t raw_result)
{
    int32_t result = (int32_t)raw_result;
    if (result < 0) {
        errno = (int)(0u - (uint32_t)result);
        return -1;
    }
    return result;
}

int access(const char *path, int mode)
{
    char normalized[SYS_PATH_MAX];
    uint32_t path_length;
    if ((mode & ~(R_OK | W_OK | X_OK)) != 0) {
        errno = EINVAL;
        return -1;
    }
    if (mode != F_OK) {
        errno = ENOTSUP;
        return -1;
    }
    if (!normalize_path(path, normalized, &path_length)) return -1;

    (void)path_length;
    return posix_result(lucid_user_syscall(
        SYSCALL_POSIX_ACCESS,
        (uint32_t)(uintptr_t)normalized,
        (uint32_t)mode,
        0,
        0
    ));
}

int open(const char *path, int flags, ...)
{
    char normalized[SYS_PATH_MAX];
    uint32_t path_length;
    if (!normalize_path(path, normalized, &path_length)) return -1;

    (void)path_length;
    return posix_result(lucid_user_syscall(
        SYSCALL_POSIX_OPEN,
        (uint32_t)(uintptr_t)normalized,
        (uint32_t)flags,
        0,
        0
    ));
}

ssize_t read(int fd, void *buffer, size_t count)
{
    return (ssize_t)posix_result(lucid_user_syscall(
        SYSCALL_POSIX_READ,
        (uint32_t)fd,
        (uint32_t)(uintptr_t)buffer,
        (uint32_t)count,
        0
    ));
}

ssize_t write(int fd, const void *buffer, size_t count)
{
    return (ssize_t)posix_result(lucid_user_syscall(
        SYSCALL_POSIX_WRITE,
        (uint32_t)fd,
        (uint32_t)(uintptr_t)buffer,
        (uint32_t)count,
        0
    ));
}

int close(int fd)
{
    return posix_result(lucid_user_syscall(
        SYSCALL_POSIX_CLOSE,
        (uint32_t)fd,
        0,
        0,
        0
    ));
}

off_t lseek(int fd, off_t offset, int whence)
{
    return (off_t)posix_result(lucid_user_syscall(
        SYSCALL_POSIX_LSEEK,
        (uint32_t)fd,
        (uint32_t)offset,
        (uint32_t)whence,
        0
    ));
}

int unlink(const char *path)
{
    char normalized[SYS_PATH_MAX];
    uint32_t path_length;
    if (!normalize_path(path, normalized, &path_length)) return -1;

    (void)path_length;
    return posix_result(lucid_user_syscall(
        SYSCALL_POSIX_UNLINK,
        (uint32_t)(uintptr_t)normalized,
        0,
        0,
        0
    ));
}

char *getcwd(char *buffer, size_t size)
{
    if (!buffer) {
        errno = EINVAL;
        return 0;
    }
    if (posix_result(lucid_user_syscall(
            SYSCALL_POSIX_GETCWD,
            (uint32_t)(uintptr_t)buffer,
            (uint32_t)size,
            0,
            0
        )) < 0) {
        return 0;
    }
    return buffer;
}
