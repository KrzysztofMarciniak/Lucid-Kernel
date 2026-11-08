#include "recipes/syscall.h"
#include "recipes/ramfs.h"
#include "recipes/terminal.h"
#include "recipes/timer.h"
#include "recipes/usermode.h"
#include "impl/syscall/internal.h"

int syscall_text_equals(const char *left, const char *right)
{
    while (*left && *left == *right) {
        left++;
        right++;
    }
    return *left == *right;
}

syscall_file_kind syscall_file_lookup(const char *path)
{
    static const char processes[] = "/system/processes";
    size_t i = 0;
    while (processes[i] && path[i] == processes[i]) i++;
    if (!processes[i]) {
        if (path[i] == '\0' ||
            (path[i] == '/' && path[i + 1] == '\0')) {
            return SYSCALL_FILE_DIRECTORY;
        }
        if (path[i] == '/') {
            const char *name = path + i + 1;
            if (syscall_text_equals(name, "list")) {
                return SYSCALL_FILE_PROCESS_LIST;
            }
            if (syscall_text_equals(name, "kill")) {
                return SYSCALL_FILE_PROCESS_KILL;
            }
            return SYSCALL_FILE_UNKNOWN;
        }
    }

    static const char prefix[] = "/system/syscalls";
    i = 0;
    while (prefix[i] && path[i] == prefix[i]) i++;
    if (prefix[i]) return SYSCALL_FILE_OUTSIDE;
    if (path[i] == '\0' ||
        (path[i] == '/' && path[i + 1] == '\0')) {
        return SYSCALL_FILE_DIRECTORY;
    }
    if (path[i] != '/') return SYSCALL_FILE_OUTSIDE;

    const char *name = path + i + 1;
    if (syscall_text_equals(name, "write")) return SYSCALL_FILE_WRITE;
    if (syscall_text_equals(name, "readkey")) return SYSCALL_FILE_READKEY;
    if (syscall_text_equals(name, "clear")) return SYSCALL_FILE_CLEAR;
    if (syscall_text_equals(name, "shutdown")) return SYSCALL_FILE_SHUTDOWN;
    if (syscall_text_equals(name, "get_ticks")) return SYSCALL_FILE_GET_TICKS;
    if (syscall_text_equals(name, "terminal_size")) {
        return SYSCALL_FILE_TERMINAL_SIZE;
    }
    if (syscall_text_equals(name, "reap")) return SYSCALL_FILE_REAP;
    return SYSCALL_FILE_UNKNOWN;
}

static size_t format_decimal(char *buffer, uint32_t number)
{
    char digits[10];
    size_t count = 0;
    do {
        digits[count++] = (char)('0' + number % 10);
        number /= 10;
    } while (number && count < sizeof(digits));
    for (size_t i = 0; i < count; i++) {
        buffer[i] = digits[count - i - 1];
    }
    return count;
}

int syscall_file_list(const char *path,
                      char *buffer,
                      size_t capacity,
                      size_t *length)
{
    static const char *const syscall_names[] = {
        "write", "readkey", "clear", "shutdown",
        "get_ticks", "terminal_size", "reap"
    };
    static const char *const process_names[] = { "kill", "list" };
    if (syscall_text_equals(path, "/system")) {
        static const char child[] = "processes\nsyscalls\n";
        if (ramfs_list(path, buffer, capacity, length) ||
            *length + sizeof(child) - 1 > capacity) {
            return -1;
        }
        for (size_t i = 0; i < sizeof(child) - 1; i++) {
            buffer[(*length)++] = child[i];
        }
        return 1;
    }

    syscall_file_kind kind = syscall_file_lookup(path);
    if (kind == SYSCALL_FILE_OUTSIDE) return 0;
    if (kind != SYSCALL_FILE_DIRECTORY) return -1;

    const char *const *names = syscall_names;
    size_t name_count = sizeof(syscall_names) / sizeof(syscall_names[0]);
    if (syscall_text_equals(path, "/system/processes") ||
        syscall_text_equals(path, "/system/processes/")) {
        names = process_names;
        name_count = sizeof(process_names) / sizeof(process_names[0]);
    }

    size_t used = 0;
    for (size_t i = 0; i < name_count; i++) {
        size_t name_length = 0;
        while (names[i][name_length]) name_length++;
        if (used + name_length + 1 > capacity) return -1;
        for (size_t j = 0; j < name_length; j++) {
            buffer[used++] = names[i][j];
        }
        buffer[used++] = '\n';
    }
    *length = used;
    return 1;
}

int syscall_file_read(const char *path,
                      char *buffer,
                      size_t capacity,
                      size_t *length)
{
    syscall_file_kind kind = syscall_file_lookup(path);
    if (kind == SYSCALL_FILE_OUTSIDE) return 0;
    if (kind == SYSCALL_FILE_UNKNOWN ||
        kind == SYSCALL_FILE_DIRECTORY) {
        return -1;
    }
    if (kind == SYSCALL_FILE_PROCESS_LIST) {
        return usermode_process_info(buffer, capacity, length) ? -1 : 1;
    }
    if (kind == SYSCALL_FILE_PROCESS_KILL) return -1;
    if ((kind == SYSCALL_FILE_READKEY || kind == SYSCALL_FILE_REAP) &&
        capacity < 11) {
        return -1;
    }

    char result[32];
    size_t result_length = 0;
    if (kind == SYSCALL_FILE_READKEY) {
        result_length = format_decimal(
            result, (uint32_t)terminal_getchar()
        );
    } else if (kind == SYSCALL_FILE_GET_TICKS) {
        result_length = format_decimal(result, timer_get_ticks());
    } else if (kind == SYSCALL_FILE_TERMINAL_SIZE) {
        uint32_t size = terminal_get_size();
        result_length = format_decimal(result, size >> 16);
        result[result_length++] = ' ';
        result_length += format_decimal(
            result + result_length, size & 0xFFFFu
        );
    } else if (kind == SYSCALL_FILE_REAP) {
        result_length = format_decimal(result, usermode_process_reap());
    } else {
        return -1;
    }

    result[result_length++] = '\n';
    if (result_length > capacity) return -1;
    for (size_t i = 0; i < result_length; i++) {
        buffer[i] = result[i];
    }
    *length = result_length;
    return 1;
}

int syscall_file_write(const char *path,
                       const char *data,
                       size_t length)
{
    syscall_file_kind kind = syscall_file_lookup(path);
    if (kind == SYSCALL_FILE_PROCESS_KILL) {
        char name[SYS_PATH_MAX];
        while (length &&
               (data[length - 1] == '\n' || data[length - 1] == '\r')) {
            length--;
        }
        if (length == 0 || length >= sizeof(name)) return -1;
        for (size_t i = 0; i < length; i++) {
            if (data[i] == '\0' || data[i] == '\n' || data[i] == '\r') {
                return -1;
            }
            name[i] = data[i];
        }
        name[length] = '\0';
        return usermode_process_kill(name) == 1 ? 1 : -1;
    }
    if (kind == SYSCALL_FILE_PROCESS_LIST) return -1;
    if (kind == SYSCALL_FILE_OUTSIDE) return 0;
    if (kind == SYSCALL_FILE_WRITE) {
        for (size_t i = 0; i < length; i++) terminal_putc(data[i]);
        return 1;
    }
    if (kind == SYSCALL_FILE_CLEAR) {
        terminal_clear();
        return 1;
    }
    if (kind == SYSCALL_FILE_SHUTDOWN) {
        syscall_poweroff_system();
    }
    return -1;
}
