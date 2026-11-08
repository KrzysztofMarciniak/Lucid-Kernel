#include "recipes/syscall.h"
#include "recipes/ramfs.h"
#include "recipes/terminal.h"
#include "recipes/timer.h"
#include "recipes/usermode.h"
#include "impl/syscall/internal.h"
#include "impl/syscall/posix/posix_internal.h"

static char terminal_screen_snapshot[SYS_FILE_MAX];
static char file_type_paths[USERMODE_PROCESS_SLOTS][SYS_PATH_MAX];
static char watch_paths[USERMODE_PROCESS_SLOTS][SYS_PATH_MAX];

int syscall_text_equals(const char *left, const char *right)
{
    while (*left && *left == *right) {
        left++;
        right++;
    }
    return *left == *right;
}

static int path_prefix(const char *path,
                       const char *prefix,
                       const char **suffix)
{
    size_t i = 0;
    while (prefix[i] && path[i] == prefix[i]) i++;
    if (prefix[i]) return 0;
    if (suffix) *suffix = path + i;
    return 1;
}

static size_t string_length(const char *text)
{
    size_t length = 0;
    while (text[length]) length++;
    return length;
}

static int copy_path(char destination[SYS_PATH_MAX],
                     const char *source,
                     size_t length)
{
    if (length == 0 || length >= SYS_PATH_MAX || source[0] != '/') {
        return 0;
    }
    for (size_t i = 0; i < length; i++) {
        if (source[i] == '\0' || source[i] == '\n' ||
            source[i] == '\r') {
            return 0;
        }
        destination[i] = source[i];
    }
    destination[length] = '\0';
    return 1;
}

static int parse_u32(const char *text, uint32_t *value)
{
    if (!text || !*text) return 0;
    uint32_t result = 0;
    size_t i = 0;
    while (text[i] >= '0' && text[i] <= '9') {
        uint32_t digit = (uint32_t)(text[i] - '0');
        if (result > (0xFFFFFFFFu - digit) / 10u) return 0;
        result = result * 10u + digit;
        i++;
    }
    if (i == 0 || (text[i] && text[i] != '\n' && text[i] != '\r')) {
        return 0;
    }
    if (text[i] == '\r') i++;
    if (text[i] == '\n') i++;
    if (text[i]) return 0;
    *value = result;
    return 1;
}

static int parse_u32_bytes(const char *text,
                           size_t length,
                           uint32_t *value)
{
    while (length &&
           (text[length - 1] == '\n' || text[length - 1] == '\r' ||
            text[length - 1] == '\0')) {
        length--;
    }
    if (length == 0) return 0;
    uint32_t result = 0;
    for (size_t i = 0; i < length; i++) {
        if (text[i] < '0' || text[i] > '9') return 0;
        uint32_t digit = (uint32_t)(text[i] - '0');
        if (result > (0xFFFFFFFFu - digit) / 10u) return 0;
        result = result * 10u + digit;
    }
    *value = result;
    return 1;
}

static int parse_i32_bytes(const char *text,
                           size_t length,
                           uint32_t *value)
{
    while (length &&
           (text[length - 1] == '\n' || text[length - 1] == '\r' ||
            text[length - 1] == '\0')) {
        length--;
    }
    if (length == 0) return 0;
    int negative = text[0] == '-';
    size_t start = negative ? 1 : 0;
    if (start == length) return 0;
    uint32_t limit = negative ? 0x80000000u : 0x7FFFFFFFu;
    uint32_t result = 0;
    for (size_t i = start; i < length; i++) {
        if (text[i] < '0' || text[i] > '9') return 0;
        uint32_t digit = (uint32_t)(text[i] - '0');
        if (result > (limit - digit) / 10u) return 0;
        result = result * 10u + digit;
    }
    *value = negative ? 0u - result : result;
    return 1;
}

static int parse_i32(const char *text, uint32_t *value)
{
    if (!text || !*text) return 0;
    if (text[0] != '-') return parse_u32(text, value);
    uint32_t magnitude;
    if (!parse_u32(text + 1, &magnitude) || magnitude > 0x80000000u) {
        return 0;
    }
    *value = 0u - magnitude;
    return 1;
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

static size_t format_signed(char *buffer, int32_t number)
{
    uint32_t magnitude = (uint32_t)number;
    size_t used = 0;
    if (number < 0) {
        buffer[used++] = '-';
        magnitude = 0u - magnitude;
    }
    return used + format_decimal(buffer + used, magnitude);
}

static int append_names(char *buffer,
                        size_t capacity,
                        size_t *used,
                        const char *const *names,
                        size_t count)
{
    for (size_t i = 0; i < count; i++) {
        size_t length = string_length(names[i]);
        if (*used + length + 1 > capacity) return 0;
        for (size_t j = 0; j < length; j++) {
            buffer[(*used)++] = names[i][j];
        }
        buffer[(*used)++] = '\n';
    }
    return 1;
}

syscall_file_kind syscall_file_lookup(const char *path)
{
    static const char *const directory_paths[] = {
        "/system", "/system/syscalls", "/system/processes",
        "/system/processes/current", "/system/processes/current/wait",
        "/system/terminals", "/system/files", "/system/time",
        "/system/time/sleep", "/system/time/wait", "/system/posix",
        "/system/posix/fd"
    };
    for (size_t i = 0; i < sizeof(directory_paths) / sizeof(directory_paths[0]); i++) {
        if (syscall_text_equals(path, directory_paths[i])) {
            return SYSCALL_FILE_DIRECTORY;
        }
    }

    if (syscall_text_equals(path, "/system/terminals/current")) {
        return SYSCALL_FILE_TERMINAL_SCREEN;
    }
    if (syscall_text_equals(path, "/system/terminals/readline")) {
        return SYSCALL_FILE_TERMINAL_READLINE;
    }
    if (syscall_text_equals(path, "/system/terminals/readline_config")) {
        return SYSCALL_FILE_TERMINAL_READLINE_CONFIG;
    }
    if (syscall_text_equals(path, "/system/processes/list")) {
        return SYSCALL_FILE_PROCESS_LIST;
    }
    if (syscall_text_equals(path, "/system/processes/kill")) {
        return SYSCALL_FILE_PROCESS_KILL;
    }
    if (syscall_text_equals(path, "/system/processes/current/fork")) {
        return SYSCALL_FILE_PROCESS_FORK;
    }
    if (syscall_text_equals(path, "/system/processes/current/exec")) {
        return SYSCALL_FILE_PROCESS_EXEC;
    }
    if (syscall_text_equals(path, "/system/processes/current/exit")) {
        return SYSCALL_FILE_PROCESS_EXIT;
    }
    if (syscall_text_equals(path, "/system/processes/current/cwd")) {
        return SYSCALL_FILE_PROCESS_CWD;
    }
    static const char wait_prefix[] = "/system/processes/current/wait/";
    const char *suffix;
    if (path_prefix(path, wait_prefix, &suffix)) {
        uint32_t pid;
        if (parse_u32(suffix, &pid)) return SYSCALL_FILE_PROCESS_WAIT;
        return SYSCALL_FILE_UNKNOWN;
    }

    static const char *const syscall_paths[] = {
        "/system/syscalls/readkey", "/system/syscalls/shutdown",
        "/system/syscalls/get_ticks", "/system/syscalls/terminal_size",
        "/system/syscalls/reap"
    };
    if (syscall_text_equals(path, syscall_paths[0])) {
        return SYSCALL_FILE_READKEY;
    }
    if (syscall_text_equals(path, syscall_paths[1])) {
        return SYSCALL_FILE_SHUTDOWN;
    }
    if (syscall_text_equals(path, syscall_paths[2])) {
        return SYSCALL_FILE_GET_TICKS;
    }
    if (syscall_text_equals(path, syscall_paths[3])) {
        return SYSCALL_FILE_TERMINAL_SIZE;
    }
    if (syscall_text_equals(path, syscall_paths[4])) {
        return SYSCALL_FILE_REAP;
    }
    if (syscall_text_equals(path, "/system/files/type")) {
        return SYSCALL_FILE_FILES_TYPE;
    }
    if (syscall_text_equals(path, "/system/files/watch")) {
        return SYSCALL_FILE_FILES_WATCH;
    }
    if (syscall_text_equals(path, "/system/files/unlink")) {
        return SYSCALL_FILE_FILES_UNLINK;
    }

    static const char wait_ticks_prefix[] = "/system/time/wait/";
    if (path_prefix(path, wait_ticks_prefix, &suffix)) {
        uint32_t ticks;
        if (parse_u32(suffix, &ticks)) return SYSCALL_FILE_TIME_WAIT;
        return SYSCALL_FILE_UNKNOWN;
    }
    static const char sleep_prefix[] = "/system/time/sleep/";
    if (path_prefix(path, sleep_prefix, &suffix)) {
        uint32_t ticks;
        if (parse_u32(suffix, &ticks)) return SYSCALL_FILE_TIME_SLEEP;
        return SYSCALL_FILE_UNKNOWN;
    }

    static const char *const posix_paths[] = {
        "/system/posix/open", "/system/posix/access",
        "/system/posix/unlink", "/system/posix/close",
        "/system/posix/lseek", "/system/posix/cwd",
        "/system/posix/error"
    };
    if (syscall_text_equals(path, posix_paths[0])) {
        return SYSCALL_FILE_POSIX_OPEN;
    }
    if (syscall_text_equals(path, posix_paths[1])) {
        return SYSCALL_FILE_POSIX_ACCESS;
    }
    if (syscall_text_equals(path, posix_paths[2])) {
        return SYSCALL_FILE_POSIX_UNLINK;
    }
    if (syscall_text_equals(path, posix_paths[3])) {
        return SYSCALL_FILE_POSIX_CLOSE;
    }
    if (syscall_text_equals(path, posix_paths[4])) {
        return SYSCALL_FILE_POSIX_LSEEK;
    }
    if (syscall_text_equals(path, posix_paths[5])) {
        return SYSCALL_FILE_POSIX_CWD;
    }
    if (syscall_text_equals(path, posix_paths[6])) {
        return SYSCALL_FILE_POSIX_ERROR;
    }
    static const char posix_fd_prefix[] = "/system/posix/fd/";
    if (path_prefix(path, posix_fd_prefix, &suffix)) {
        uint32_t fd;
        if (parse_i32(suffix, &fd)) return SYSCALL_FILE_POSIX_FD;
        return SYSCALL_FILE_UNKNOWN;
    }

    static const char *const virtual_prefixes[] = {
        "/system/syscalls/", "/system/processes/", "/system/terminals/",
        "/system/files/", "/system/time/", "/system/posix/"
    };
    for (size_t i = 0; i < sizeof(virtual_prefixes) / sizeof(virtual_prefixes[0]); i++) {
        if (path_prefix(path, virtual_prefixes[i], 0)) {
            return SYSCALL_FILE_UNKNOWN;
        }
    }
    return SYSCALL_FILE_OUTSIDE;
}

static int syscall_file_list(const char *path,
                             char *buffer,
                             size_t capacity,
                             size_t *length)
{
    static const char *const syscall_names[] = {
        "get_ticks", "readkey", "reap", "shutdown", "terminal_size"
    };
    static const char *const process_names[] = { "current", "kill", "list" };
    static const char *const current_process_names[] = {
        "cwd", "exec", "exit", "fork", "wait"
    };
    static const char *const terminal_names[] = {
        "current", "readline", "readline_config"
    };
    static const char *const file_names[] = { "type", "unlink", "watch" };
    static const char *const time_names[] = { "sleep", "wait" };
    static const char *const posix_names[] = {
        "access", "close", "cwd", "error", "fd", "lseek", "open", "unlink"
    };
    size_t used = 0;
    if (syscall_text_equals(path, "/system")) {
        static const char child[] =
            "files\nposix\nprocesses\nsyscalls\nterminals\ntime\n";
        if (ramfs_list(path, buffer, capacity, length) ||
            *length + sizeof(child) - 1 > capacity) {
            return -1;
        }
        for (size_t i = 0; i < sizeof(child) - 1; i++) {
            buffer[(*length)++] = child[i];
        }
        return 1;
    }

    const char *const *names = 0;
    size_t count = 0;
    if (syscall_text_equals(path, "/system/syscalls")) {
        names = syscall_names;
        count = sizeof(syscall_names) / sizeof(syscall_names[0]);
    } else if (syscall_text_equals(path, "/system/processes")) {
        names = process_names;
        count = sizeof(process_names) / sizeof(process_names[0]);
    } else if (syscall_text_equals(path, "/system/processes/current")) {
        names = current_process_names;
        count = sizeof(current_process_names) /
            sizeof(current_process_names[0]);
    } else if (syscall_text_equals(
                   path, "/system/processes/current/wait") ||
               syscall_text_equals(path, "/system/time/sleep") ||
               syscall_text_equals(path, "/system/time/wait") ||
               syscall_text_equals(path, "/system/posix/fd")) {
        count = 0;
    } else if (syscall_text_equals(path, "/system/terminals")) {
        names = terminal_names;
        count = sizeof(terminal_names) / sizeof(terminal_names[0]);
    } else if (syscall_text_equals(path, "/system/files")) {
        names = file_names;
        count = sizeof(file_names) / sizeof(file_names[0]);
    } else if (syscall_text_equals(path, "/system/time")) {
        names = time_names;
        count = sizeof(time_names) / sizeof(time_names[0]);
    } else if (syscall_text_equals(path, "/system/posix")) {
        names = posix_names;
        count = sizeof(posix_names) / sizeof(posix_names[0]);
    } else {
        return -1;
    }
    if (!append_names(buffer, capacity, &used, names, count)) return -1;
    *length = used;
    return 1;
}

static int copy_text(char *buffer,
                     size_t capacity,
                     size_t *length,
                     const char *text,
                     size_t text_length)
{
    if (text_length > capacity) return 0;
    for (size_t i = 0; i < text_length; i++) buffer[i] = text[i];
    *length = text_length;
    return 1;
}

int syscall_file_read(const char *path,
                      char *buffer,
                      size_t capacity,
                      size_t *length,
                      syscall_frame *frame)
{
    syscall_file_kind kind = syscall_file_lookup(path);
    if (kind == SYSCALL_FILE_OUTSIDE) return 0;
    if (kind == SYSCALL_FILE_UNKNOWN ||
        kind == SYSCALL_FILE_TERMINAL_READLINE ||
        kind == SYSCALL_FILE_TERMINAL_READLINE_CONFIG ||
        kind == SYSCALL_FILE_PROCESS_KILL ||
        kind == SYSCALL_FILE_PROCESS_EXEC ||
        kind == SYSCALL_FILE_PROCESS_EXIT ||
        kind == SYSCALL_FILE_FILES_UNLINK ||
        kind == SYSCALL_FILE_POSIX_OPEN ||
        kind == SYSCALL_FILE_POSIX_ACCESS ||
        kind == SYSCALL_FILE_POSIX_UNLINK ||
        kind == SYSCALL_FILE_POSIX_CLOSE ||
        kind == SYSCALL_FILE_POSIX_LSEEK) {
        return -1;
    }
    if (kind == SYSCALL_FILE_DIRECTORY) {
        return syscall_file_list(path, buffer, capacity, length);
    }
    if (kind == SYSCALL_FILE_PROCESS_LIST) {
        return usermode_process_info(buffer, capacity, length) ? -1 : 1;
    }
    if (kind == SYSCALL_FILE_TERMINAL_SCREEN) {
        return terminal_screen_read(buffer, capacity, length) ? 1 : -1;
    }
    if (kind == SYSCALL_FILE_PROCESS_FORK) {
        if (capacity < 11) return -1;
        uint32_t pid = usermode_process_fork(frame);
        if (pid == SYS_UNKNOWN_TEST) return -1;
        *length = format_decimal(buffer, pid);
        buffer[(*length)++] = '\n';
        return 1;
    }
    if (kind == SYSCALL_FILE_PROCESS_WAIT) {
        const char *pid_text =
            path + string_length("/system/processes/current/wait/");
        uint32_t pid;
        if (!parse_u32(pid_text, &pid)) return -1;
        return usermode_process_wait(pid, frame) ? 3 : 2;
    }
    if (kind == SYSCALL_FILE_TIME_WAIT ||
        kind == SYSCALL_FILE_TIME_SLEEP) {
        const char *ticks_text = path +
            string_length(kind == SYSCALL_FILE_TIME_WAIT
                ? "/system/time/wait/"
                : "/system/time/sleep/");
        uint32_t ticks;
        if (!parse_u32(ticks_text, &ticks)) return -1;
        if (usermode_process_wait_ticks(
                ticks, kind == SYSCALL_FILE_TIME_SLEEP, frame)) {
            return 3;
        }
        return 2;
    }
    if (kind == SYSCALL_FILE_FILES_WATCH) {
        uint32_t slot = usermode_process_current_slot();
        const char *watched_path = watch_paths[slot];
        if (!watched_path[0] || !ramfs_is_watchable(watched_path)) {
            return -1;
        }
        usermode_process_watch_file(watched_path, frame);
        return 3;
    }
    if (kind == SYSCALL_FILE_FILES_TYPE) {
        uint32_t slot = usermode_process_current_slot();
        const char *typed_path = file_type_paths[slot];
        int is_directory;
        if (!typed_path[0] || capacity < 2) return -1;
        syscall_file_kind typed_kind = syscall_file_lookup(typed_path);
        if (typed_kind == SYSCALL_FILE_DIRECTORY) {
            is_directory = 1;
        } else if (typed_kind != SYSCALL_FILE_OUTSIDE &&
                   typed_kind != SYSCALL_FILE_UNKNOWN) {
            is_directory = 0;
        } else if (typed_kind == SYSCALL_FILE_UNKNOWN) {
            return -1;
        } else {
            is_directory = ramfs_is_directory(typed_path);
            if (is_directory < 0) return -1;
        }
        buffer[0] = is_directory ? '1' : '0';
        buffer[1] = '\n';
        *length = 2;
        return 1;
    }
    if (kind == SYSCALL_FILE_PROCESS_CWD ||
        kind == SYSCALL_FILE_POSIX_CWD) {
        return copy_text(buffer, capacity, length, "/\n", 2) ? 1 : -1;
    }
    if (kind == SYSCALL_FILE_POSIX_ERROR) {
        int32_t error = syscall_posix_last_error(
            syscall_posix_current_slot()
        );
        if (error <= 0) return -1;
        size_t used = format_decimal(buffer, (uint32_t)error);
        buffer[used++] = '\n';
        if (used > capacity) return -1;
        *length = used;
        return 1;
    }
    if (kind == SYSCALL_FILE_POSIX_FD) {
        uint32_t fd;
        const char *fd_text =
            path + string_length("/system/posix/fd/");
        if (!parse_i32(fd_text, &fd)) return -1;
        syscall_frame posix_frame = *frame;
        posix_frame.ebx = fd;
        posix_frame.ecx = frame->edx;
        posix_frame.edx = frame->esi;
        syscall_posix_read_handle(&posix_frame);
        syscall_posix_set_last_error(
            syscall_posix_current_slot(),
            (int32_t)posix_frame.eax
        );
        frame->eax = posix_frame.eax;
        return 2;
    }

    if ((kind == SYSCALL_FILE_READKEY || kind == SYSCALL_FILE_REAP) &&
        capacity < 11) {
        return -1;
    }

    char result[32];
    size_t result_length = 0;
    if (kind == SYSCALL_FILE_READKEY) {
        result_length = format_signed(result, terminal_getchar());
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
    return copy_text(buffer, capacity, length, result, result_length) ? 1 : -1;
}

static int32_t call_posix_handler(posix_file_operation operation,
                                  syscall_frame *frame)
{
    switch (operation) {
    case POSIX_FILE_OPEN: syscall_posix_open_handle(frame); break;
    case POSIX_FILE_ACCESS: syscall_posix_access_handle(frame); break;
    case POSIX_FILE_UNLINK: syscall_posix_unlink_handle(frame); break;
    case POSIX_FILE_CLOSE: syscall_posix_close_handle(frame); break;
    case POSIX_FILE_LSEEK: syscall_posix_lseek_handle(frame); break;
    case POSIX_FILE_WRITE: syscall_posix_write_handle(frame); break;
    default: return -1;
    }
    int32_t result = (int32_t)frame->eax;
    syscall_posix_set_last_error(syscall_posix_current_slot(), result);
    return result;
}

int syscall_file_write(const char *path,
                       const char *data,
                       size_t length,
                       syscall_frame *frame,
                       uint32_t *result)
{
    syscall_file_kind kind = syscall_file_lookup(path);
    if (kind == SYSCALL_FILE_PROCESS_KILL) {
        char name[SYS_PATH_MAX];
        while (length &&
               (data[length - 1] == '\n' || data[length - 1] == '\r')) {
            length--;
        }
        if (!copy_path(name, data, length)) return -1;
        *result = length;
        return usermode_process_kill(name) == 1 ? 1 : -1;
    }
    if (kind == SYSCALL_FILE_PROCESS_LIST) return -1;
    if (kind == SYSCALL_FILE_OUTSIDE) return 0;
    if (kind == SYSCALL_FILE_TERMINAL_SCREEN) {
        if (length == 0) {
            terminal_clear();
            *result = 0;
            return 1;
        }
        size_t current_length = 0;
        if (!terminal_screen_read(terminal_screen_snapshot,
                                  sizeof(terminal_screen_snapshot),
                                  &current_length)) {
            return -1;
        }
        size_t start = 0;
        if (current_length < sizeof(terminal_screen_snapshot) &&
            length >= current_length) {
            start = current_length;
            for (size_t i = 0; i < current_length; i++) {
                if (terminal_screen_snapshot[i] != data[i]) {
                    start = 0;
                    break;
                }
            }
        }
        if (start == 0) terminal_clear();
        for (size_t i = start; i < length; i++) terminal_putc(data[i]);
        *result = length;
        return 1;
    }
    if (kind == SYSCALL_FILE_TERMINAL_READLINE_CONFIG) {
        if (!syscall_readline_set_completion(data, length)) return -1;
        *result = length;
        return 1;
    }
    if (kind == SYSCALL_FILE_SHUTDOWN) {
        syscall_poweroff_system();
    }
    if (kind == SYSCALL_FILE_PROCESS_EXIT) {
        uint32_t status;
        if (!parse_u32_bytes(data, length, &status)) return -1;
        usermode_process_exit(status);
        return 3;
    }
    if (kind == SYSCALL_FILE_PROCESS_EXEC) {
        char image_path[SYS_PATH_MAX];
        while (length &&
               (data[length - 1] == '\n' || data[length - 1] == '\r')) {
            length--;
        }
        if (!copy_path(image_path, data, length)) return -1;
        int exec_result = usermode_process_exec(image_path, frame);
        if (exec_result < 0) return 3;
        if (exec_result == 0) return -1;
        *result = 0;
        return 1;
    }
    if (kind == SYSCALL_FILE_FILES_TYPE) {
        uint32_t slot = usermode_process_current_slot();
        if (!copy_path(file_type_paths[slot], data, length)) return -1;
        *result = length;
        return 1;
    }
    if (kind == SYSCALL_FILE_FILES_WATCH) {
        uint32_t slot = usermode_process_current_slot();
        if (!copy_path(watch_paths[slot], data, length) ||
            !ramfs_is_watchable(watch_paths[slot])) {
            return -1;
        }
        *result = length;
        return 1;
    }
    if (kind == SYSCALL_FILE_FILES_UNLINK) {
        if (length == 0 || data[length - 1] != '\0') return -1;
        syscall_frame posix_frame = *frame;
        posix_frame.ebx = frame->edx;
        int32_t unlink_result = call_posix_handler(
            POSIX_FILE_UNLINK, &posix_frame
        );
        *result = (uint32_t)unlink_result;
        return 1;
    }
    if (kind == SYSCALL_FILE_POSIX_OPEN) {
        if (length < sizeof(uint32_t) + 2 ||
            data[sizeof(uint32_t)] != '/' || data[length - 1] != '\0') {
            return -1;
        }
        syscall_frame posix_frame = *frame;
        posix_frame.ebx = frame->edx + sizeof(uint32_t);
        posix_frame.ecx =
            (uint32_t)(uint8_t)data[0] |
            ((uint32_t)(uint8_t)data[1] << 8) |
            ((uint32_t)(uint8_t)data[2] << 16) |
            ((uint32_t)(uint8_t)data[3] << 24);
        *result = (uint32_t)call_posix_handler(
            POSIX_FILE_OPEN, &posix_frame
        );
        return 1;
    }
    if (kind == SYSCALL_FILE_POSIX_ACCESS) {
        if (length < sizeof(uint32_t) + 2 ||
            data[sizeof(uint32_t)] != '/' || data[length - 1] != '\0') {
            return -1;
        }
        syscall_frame posix_frame = *frame;
        posix_frame.ebx = frame->edx + sizeof(uint32_t);
        posix_frame.ecx =
            (uint32_t)(uint8_t)data[0] |
            ((uint32_t)(uint8_t)data[1] << 8) |
            ((uint32_t)(uint8_t)data[2] << 16) |
            ((uint32_t)(uint8_t)data[3] << 24);
        *result = (uint32_t)call_posix_handler(
            POSIX_FILE_ACCESS, &posix_frame
        );
        return 1;
    }
    if (kind == SYSCALL_FILE_POSIX_UNLINK) {
        if (length < 2 || data[length - 1] != '\0') return -1;
        syscall_frame posix_frame = *frame;
        posix_frame.ebx = frame->edx;
        *result = (uint32_t)call_posix_handler(
            POSIX_FILE_UNLINK, &posix_frame
        );
        return 1;
    }
    if (kind == SYSCALL_FILE_POSIX_CLOSE) {
        uint32_t fd;
        if (!parse_i32_bytes(data, length, &fd)) return -1;
        syscall_frame posix_frame = *frame;
        posix_frame.ebx = fd;
        *result = (uint32_t)call_posix_handler(
            POSIX_FILE_CLOSE, &posix_frame
        );
        return 1;
    }
    if (kind == SYSCALL_FILE_POSIX_LSEEK) {
        uint32_t fd;
        uint32_t whence;
        char request[40];
        if (length == 0 || length >= sizeof(request)) return -1;
        for (size_t i = 0; i < length; i++) request[i] = data[i];
        request[length] = '\0';
        const char *offset_text = request;
        size_t offset = 0;
        while (offset < length && request[offset] != ' ') offset++;
        if (offset == length) return -1;
        char fd_text[12];
        if (offset == 0 || offset >= sizeof(fd_text)) return -1;
        for (size_t i = 0; i < offset; i++) fd_text[i] = request[i];
        fd_text[offset] = '\0';
        if (!parse_i32(fd_text, &fd)) return -1;
        offset_text = request + offset + 1;
        size_t second_space = offset + 1;
        while (second_space < length && request[second_space] != ' ') {
            second_space++;
        }
        if (second_space == length || second_space == offset + 1) return -1;
        char signed_offset[12];
        size_t signed_length = second_space - offset - 1;
        if (signed_length >= sizeof(signed_offset)) return -1;
        for (size_t i = 0; i < signed_length; i++) {
            signed_offset[i] = offset_text[i];
        }
        signed_offset[signed_length] = '\0';
        int negative = signed_offset[0] == '-';
        size_t digit_start = negative ? 1 : 0;
        uint32_t magnitude;
        if (!parse_u32(signed_offset + digit_start, &magnitude) ||
            magnitude > (negative ? 0x80000000u : 0x7FFFFFFFu)) {
            return -1;
        }
        uint32_t signed_value = negative ? 0u - magnitude : magnitude;
        if (!parse_i32(request + second_space + 1, &whence)) return -1;
        syscall_frame posix_frame = *frame;
        posix_frame.ebx = fd;
        posix_frame.ecx = signed_value;
        posix_frame.edx = whence;
        *result = (uint32_t)call_posix_handler(
            POSIX_FILE_LSEEK, &posix_frame
        );
        return 1;
    }
    if (kind == SYSCALL_FILE_POSIX_FD) {
        uint32_t fd;
        const char *fd_text = path + string_length("/system/posix/fd/");
        if (!parse_i32(fd_text, &fd)) return -1;
        syscall_frame posix_frame = *frame;
        posix_frame.ebx = fd;
        posix_frame.ecx = frame->edx;
        posix_frame.edx = frame->esi;
        *result = (uint32_t)call_posix_handler(
            POSIX_FILE_WRITE, &posix_frame
        );
        return 1;
    }
    return -1;
}
