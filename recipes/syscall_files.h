#ifndef LUCID_RECIPE_SYSCALL_FILES_H
#define LUCID_RECIPE_SYSCALL_FILES_H

#include <lucid.h>
#include "syscall_abi.h"
#include "syscall_fs_read.h"
#include "syscall_fs_write.h"
#include <stddef.h>

#ifdef LUCID_USER
static inline int lucid_parse_u32(const char *text,
                                  uint32_t length,
                                  uint32_t *value)
{
    if (length == 0) return 0;
    uint32_t result = 0;
    uint32_t i = 0;
    while (i < length && text[i] >= '0' && text[i] <= '9') {
        uint32_t digit = (uint32_t)(text[i] - '0');
        if (result > (0xFFFFFFFFu - digit) / 10u) return 0;
        result = result * 10u + digit;
        i++;
    }
    if (i == 0) return 0;
    while (i < length && (text[i] == '\r' || text[i] == '\n')) i++;
    if (i != length) return 0;
    *value = result;
    return 1;
}

static inline uint32_t lucid_file_number(const char *path)
{
    char text[16];
    uint32_t length = lucid_fs_read(path, text, sizeof(text));
    uint32_t value;
    return length == SYS_UNKNOWN_TEST ||
        !lucid_parse_u32(text, length, &value)
        ? SYS_UNKNOWN_TEST
        : value;
}

static inline size_t lucid_format_u32(char *buffer, uint32_t number)
{
    char digits[10];
    size_t count = 0;
    do {
        digits[count++] = (char)('0' + number % 10);
        number /= 10;
    } while (number);
    for (size_t i = 0; i < count; i++) {
        buffer[i] = digits[count - i - 1];
    }
    return count;
}

static inline uint32_t lucid_process_fork(void)
{
    char text[16];
    uint32_t length = lucid_fs_read(
        "/system/processes/current/fork", text, sizeof(text)
    );
    if (length == 0) return 0;
    uint32_t pid;
    return length == SYS_UNKNOWN_TEST ||
        !lucid_parse_u32(text, length, &pid)
        ? SYS_UNKNOWN_TEST
        : pid;
}

static inline uint32_t lucid_process_exec(const char *path,
                                         uint32_t length)
{
    return lucid_fs_write(
        "/system/processes/current/exec", path, length
    );
}

static inline void lucid_process_exit(uint32_t status)
{
    char text[11];
    size_t length = lucid_format_u32(text, status);
    text[length++] = '\n';
    (void)lucid_fs_write("/system/processes/current/exit", text,
                         (uint32_t)length);
}

static inline uint32_t lucid_process_wait(uint32_t pid)
{
    char path[48];
    static const char prefix[] = "/system/processes/current/wait/";
    size_t used = 0;
    while (prefix[used]) {
        path[used] = prefix[used];
        used++;
    }
    used += lucid_format_u32(path + used, pid);
    path[used] = '\0';
    char unused[1];
    return lucid_fs_read(path, unused, 0);
}

static inline uint32_t lucid_process_reap(void)
{
    return lucid_file_number("/system/syscalls/reap");
}

static inline uint32_t lucid_get_ticks(void)
{
    return lucid_file_number("/system/syscalls/get_ticks");
}

static inline uint32_t lucid_read_key(void)
{
    return lucid_file_number("/system/syscalls/readkey");
}

static inline uint32_t lucid_terminal_size(void)
{
    char text[24];
    uint32_t length = lucid_fs_read(
        "/system/syscalls/terminal_size", text, sizeof(text)
    );
    if (length == SYS_UNKNOWN_TEST) return SYS_UNKNOWN_TEST;
    uint32_t columns = 0;
    uint32_t rows = 0;
    uint32_t i = 0;
    while (i < length && text[i] >= '0' && text[i] <= '9') {
        columns = columns * 10u + (uint32_t)(text[i++] - '0');
    }
    if (i == 0 || i >= length || text[i++] != ' ') {
        return SYS_UNKNOWN_TEST;
    }
    uint32_t start = i;
    while (i < length && text[i] >= '0' && text[i] <= '9') {
        rows = rows * 10u + (uint32_t)(text[i++] - '0');
    }
    while (i < length && (text[i] == '\n' || text[i] == '\r')) i++;
    if (i != length || i == start) return SYS_UNKNOWN_TEST;
    return (columns << 16) | (rows & 0xFFFFu);
}

static inline uint32_t lucid_wait_ticks(uint32_t ticks)
{
    char path[32];
    static const char prefix[] = "/system/time/wait/";
    size_t used = 0;
    while (prefix[used]) {
        path[used] = prefix[used];
        used++;
    }
    used += lucid_format_u32(path + used, ticks);
    path[used] = '\0';
    char unused[1];
    return lucid_fs_read(path, unused, 0);
}

static inline uint32_t lucid_sleep(uint32_t ticks)
{
    char path[32];
    static const char prefix[] = "/system/time/sleep/";
    size_t used = 0;
    while (prefix[used]) {
        path[used] = prefix[used];
        used++;
    }
    used += lucid_format_u32(path + used, ticks);
    path[used] = '\0';
    char unused[1];
    return lucid_fs_read(path, unused, 0);
}

static inline uint32_t lucid_fs_watch(const char *path)
{
    uint32_t length = 0;
    while (path[length]) length++;
    uint32_t result = lucid_fs_write(
        "/system/files/watch", path, length
    );
    if (result == SYS_UNKNOWN_TEST) return result;
    char unused[1];
    return lucid_fs_read("/system/files/watch", unused, 0);
}

static inline int lucid_fs_is_directory(const char *path)
{
    uint32_t path_length = 0;
    while (path[path_length]) path_length++;
    uint32_t result = lucid_fs_write(
        "/system/files/type", path, path_length
    );
    if (result == SYS_UNKNOWN_TEST) return -1;
    char text[4];
    uint32_t length = lucid_fs_read("/system/files/type", text, sizeof(text));
    if (length != 2 || text[1] != '\n' ||
        (text[0] != '0' && text[0] != '1')) {
        return -1;
    }
    return text[0] == '1';
}

static inline uint32_t lucid_write(const void *bytes, uint32_t length)
{
    static char screen[SYS_FILE_MAX];
    static const char path[] = "/system/terminals/current";
    if (length > SYS_FILE_MAX) return SYS_UNKNOWN_TEST;
    uint32_t screen_length = lucid_fs_read(path, screen, sizeof(screen));
    if (screen_length == SYS_UNKNOWN_TEST ||
        screen_length > sizeof(screen)) {
        return SYS_UNKNOWN_TEST;
    }
    if (screen_length + length > sizeof(screen)) {
        size_t drop = screen_length + length - sizeof(screen);
        while (drop < screen_length && screen[drop] != '\n') drop++;
        if (drop < screen_length) drop++;
        if (drop > screen_length) drop = screen_length;
        screen_length -= (uint32_t)drop;
        for (size_t i = 0; i < screen_length; i++) {
            screen[i] = screen[i + drop];
        }
    }
    const char *input = (const char *)bytes;
    for (uint32_t i = 0; i < length; i++) {
        screen[screen_length + i] = input[i];
    }
    uint32_t result = lucid_fs_write(
        path, screen, screen_length + length
    );
    return result == SYS_UNKNOWN_TEST ? result : length;
}
#endif

#endif
