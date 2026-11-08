#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>

#include "recipes/syscall.h"
#include "internal.h"

int errno;

static uint32_t syscall_transport(uint32_t number,
                                 uint32_t arg1,
                                 uint32_t arg2,
                                 uint32_t arg3,
                                 uint32_t arg4)
{
#ifdef LUCID_USER_SYSCALL_TEST
    return lucid_libc_test_transport(number, arg1, arg2, arg3, arg4);
#else
    uint32_t result;
    __asm__ volatile (
        "int $0x80"
        : "=a"(result)
        : "0"(number), "b"(arg1), "c"(arg2), "d"(arg3), "S"(arg4)
        : "memory"
    );
    return result;
#endif
}

uint32_t lucid_fs_read(const char *path, void *buffer, uint32_t capacity)
{
    uint32_t path_length = 0;
    while (path[path_length]) path_length++;
    return syscall_transport(
        SYS_FS_READ,
        (uint32_t)(uintptr_t)path,
        path_length,
        (uint32_t)(uintptr_t)buffer,
        capacity
    );
}

uint32_t lucid_fs_write(const char *path,
                        const void *data,
                        uint32_t length)
{
    uint32_t path_length = 0;
    while (path[path_length]) path_length++;
    return syscall_transport(
        SYS_FS_WRITE,
        (uint32_t)(uintptr_t)path,
        path_length,
        (uint32_t)(uintptr_t)data,
        length
    );
}

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
    if (raw_result == SYS_UNKNOWN_TEST) {
        errno = EIO;
        return -1;
    }
    if (result < 0) {
        errno = (int)(0u - (uint32_t)result);
        return -1;
    }
    return result;
}

static size_t append_u32(char *buffer, size_t used, uint32_t value)
{
    used += lucid_format_u32(buffer + used, value);
    return used;
}

static size_t append_signed(char *buffer, size_t used, int32_t value)
{
    uint32_t magnitude = (uint32_t)value;
    if (value < 0) {
        buffer[used++] = '-';
        magnitude = 0u - magnitude;
    }
    return append_u32(buffer, used, magnitude);
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

    uint8_t request[sizeof(uint32_t) + SYS_PATH_MAX];
    uint32_t mode_bits = (uint32_t)mode;
    request[0] = (uint8_t)mode_bits;
    request[1] = (uint8_t)(mode_bits >> 8);
    request[2] = (uint8_t)(mode_bits >> 16);
    request[3] = (uint8_t)(mode_bits >> 24);
    for (uint32_t i = 0; i <= path_length; i++) {
        request[sizeof(uint32_t) + i] = (uint8_t)normalized[i];
    }
    return posix_result(lucid_fs_write(
        "/system/posix/access",
        request,
        sizeof(uint32_t) + path_length + 1
    ));
}

int open(const char *path, int flags, ...)
{
    char normalized[SYS_PATH_MAX];
    uint32_t path_length;
    if (!normalize_path(path, normalized, &path_length)) return -1;

    uint8_t request[sizeof(uint32_t) + SYS_PATH_MAX];
    uint32_t flag_bits = (uint32_t)flags;
    request[0] = (uint8_t)flag_bits;
    request[1] = (uint8_t)(flag_bits >> 8);
    request[2] = (uint8_t)(flag_bits >> 16);
    request[3] = (uint8_t)(flag_bits >> 24);
    for (uint32_t i = 0; i <= path_length; i++) {
        request[sizeof(uint32_t) + i] = (uint8_t)normalized[i];
    }
    return posix_result(lucid_fs_write(
        "/system/posix/open",
        request,
        sizeof(uint32_t) + path_length + 1
    ));
}

ssize_t read(int fd, void *buffer, size_t count)
{
    if (count > SYS_FILE_MAX) count = SYS_FILE_MAX;
    char path[40];
    static const char prefix[] = "/system/posix/fd/";
    size_t used = 0;
    while (prefix[used]) {
        path[used] = prefix[used];
        used++;
    }
    used = append_signed(path, used, fd);
    path[used] = '\0';
    return (ssize_t)posix_result(lucid_fs_read(
        path, buffer, (uint32_t)count
    ));
}

ssize_t write(int fd, const void *buffer, size_t count)
{
    if (count > SYS_FILE_MAX) count = SYS_FILE_MAX;
    char path[40];
    static const char prefix[] = "/system/posix/fd/";
    size_t used = 0;
    while (prefix[used]) {
        path[used] = prefix[used];
        used++;
    }
    used = append_signed(path, used, fd);
    path[used] = '\0';
    return (ssize_t)posix_result(lucid_fs_write(
        path, buffer, (uint32_t)count
    ));
}

int close(int fd)
{
    char request[12];
    size_t length = append_signed(request, 0, fd);
    request[length++] = '\n';
    return posix_result(lucid_fs_write(
        "/system/posix/close", request, (uint32_t)length
    ));
}

off_t lseek(int fd, off_t offset, int whence)
{
    char request[36];
    size_t length = append_signed(request, 0, fd);
    request[length++] = ' ';
    length = append_signed(request, length, (int32_t)offset);
    request[length++] = ' ';
    length = append_signed(request, length, whence);
    request[length++] = '\n';
    return (off_t)posix_result(lucid_fs_write(
        "/system/posix/lseek", request, (uint32_t)length
    ));
}

int unlink(const char *path)
{
    char normalized[SYS_PATH_MAX];
    uint32_t path_length;
    if (!normalize_path(path, normalized, &path_length)) return -1;
    return posix_result(lucid_fs_write(
        "/system/posix/unlink",
        normalized,
        path_length + 1
    ));
}

char *getcwd(char *buffer, size_t size)
{
    if (!buffer) {
        errno = EINVAL;
        return 0;
    }
    if (size < 2) {
        errno = ERANGE;
        return 0;
    }
    buffer[0] = '/';
    buffer[1] = '\0';
    return buffer;
}
