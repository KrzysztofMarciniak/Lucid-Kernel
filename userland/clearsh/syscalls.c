#include "clearsh_internal.h"

uint32_t clearsh_syscall(uint32_t number,
                         uint32_t arg1,
                         uint32_t arg2,
                         uint32_t arg3,
                         uint32_t arg4)
{
    return lucid_user_syscall(number, arg1, arg2, arg3, arg4);
}

void terminal_write(const char *text)
{
    size_t length = 0;
    while (text[length]) length++;
    clearsh_syscall(SYS_WRITE, (uint32_t)text, length, 0, 0);
}

void terminal_putc(char c)
{
    clearsh_syscall(SYS_WRITE, (uint32_t)&c, 1, 0, 0);
}

uint32_t terminal_read_key(void)
{
    return clearsh_syscall(SYS_READKEY, 0, 0, 0, 0);
}

uint32_t terminal_get_size(void)
{
    return clearsh_syscall(SYS_GET_TERMINAL_SIZE, 0, 0, 0, 0);
}

void terminal_clear(void)
{
    clearsh_syscall(SYS_CLEAR, 0, 0, 0, 0);
}

int ramfs_read(const char *path,
               char *buffer,
               size_t capacity,
               size_t *length)
{
    size_t path_length = 0;
    while (path[path_length]) path_length++;
    uint32_t result = clearsh_syscall(
        SYS_FS_READ,
        (uint32_t)path,
        path_length,
        (uint32_t)buffer,
        capacity
    );
    if (result == SYS_UNKNOWN_TEST || result > capacity) return 1;
    *length = result;
    return 0;
}

int ramfs_write(const char *path, const char *data, size_t length)
{
    size_t path_length = 0;
    while (path[path_length]) path_length++;
    return clearsh_syscall(
        SYS_FS_WRITE,
        (uint32_t)path,
        path_length,
        (uint32_t)data,
        length
    ) == SYS_UNKNOWN_TEST;
}

int ramfs_list(const char *path,
               char *buffer,
               size_t capacity,
               size_t *length)
{
    size_t path_length = 0;
    while (path[path_length]) path_length++;
    uint32_t result = clearsh_syscall(
        SYS_FS_LIST,
        (uint32_t)path,
        path_length,
        (uint32_t)buffer,
        capacity
    );
    if (result == SYS_UNKNOWN_TEST || result > capacity) return 1;
    *length = result;
    return 0;
}

int ramfs_is_directory(const char *path)
{
    size_t path_length = 0;
    while (path[path_length]) path_length++;
    uint32_t result = clearsh_syscall(
        SYS_FS_STAT,
        (uint32_t)path,
        path_length,
        0,
        0
    );
    if (result == SYS_UNKNOWN_TEST) return -1;
    return result ? 1 : 0;
}
