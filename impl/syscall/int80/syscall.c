#include "recipes/syscall.h"
#include "recipes/idt.h"
#include "recipes/ramfs.h"
#include "recipes/status.h"
#include "recipes/terminal.h"
#include "recipes/timer.h"
#include "recipes/usermode.h"
#include "recipes/vmm.h"
#include "impl/syscall/internal.h"

extern void syscall_isr(void);
extern const syscall_handler_entry __start_syscall_handlers[];
extern const syscall_handler_entry __stop_syscall_handlers[];

#define COMMAND_HISTORY_SIZE 8
#define COMMAND_HISTORY_LINE_SIZE SYS_LINE_MAX

typedef struct {
    char command_history[COMMAND_HISTORY_SIZE][COMMAND_HISTORY_LINE_SIZE];
    uint32_t command_history_count;
    char command_history_draft[COMMAND_HISTORY_LINE_SIZE];
    int command_history_draft_valid;
    uint16_t input_open_positions[SYS_LINE_MAX];
    sys_readline_completion readline_completion;
} readline_state;

static readline_state readline_states[USERMODE_PROCESS_SLOTS];
static char filesystem_buffers[USERMODE_PROCESS_SLOTS][SYS_FILE_MAX];
static char completion_probe[SYS_FILE_MAX];
static char completion_matches[SYS_FILE_MAX];

#define READLINE_STATE \
    readline_states[usermode_process_current_slot()]
#define filesystem_buffer \
    filesystem_buffers[usermode_process_current_slot()]
#define command_history (READLINE_STATE.command_history)
#define command_history_count (READLINE_STATE.command_history_count)
#define command_history_draft (READLINE_STATE.command_history_draft)
#define command_history_draft_valid \
    (READLINE_STATE.command_history_draft_valid)
#define input_open_positions (READLINE_STATE.input_open_positions)
#define readline_completion (READLINE_STATE.readline_completion)

static void replace_input_line(char *buffer,
                               uint32_t capacity,
                               uint32_t *length,
                               uint32_t *cursor,
                               const char *replacement)
{
    while (*cursor) {
        terminal_putc('\b');
        (*cursor)--;
    }
    for (uint32_t i = 0; i < *length; i++) {
        terminal_putc(' ');
    }
    for (uint32_t i = 0; i < *length; i++) {
        terminal_putc('\b');
    }
    *length = 0;

    while (replacement[*length] && *length + 1 < capacity) {
        buffer[*length] = replacement[*length];
        terminal_putc(replacement[*length]);
        (*length)++;
    }
    buffer[*length] = '\0';
    *cursor = *length;
}

static int insert_input(char *buffer,
                        uint32_t capacity,
                        uint32_t *length,
                        uint32_t *cursor,
                        const char *text,
                        uint32_t text_length,
                        uint32_t cursor_advance)
{
    if (*length + text_length >= capacity) return 0;
    for (uint32_t i = *length; i > *cursor; i--) {
        buffer[i + text_length - 1] = buffer[i - 1];
    }
    for (uint32_t i = 0; i < text_length; i++) {
        buffer[*cursor + i] = text[i];
    }

    uint32_t start = *cursor;
    *length += text_length;
    *cursor += cursor_advance;
    for (uint32_t i = start; i < *length; i++) {
        terminal_putc(buffer[i]);
    }
    for (uint32_t i = *cursor; i < *length; i++) {
        terminal_putc('\b');
    }
    return 1;
}

static void delete_input(char *buffer,
                         uint32_t *length,
                         uint32_t *cursor,
                         int before_cursor)
{
    if (before_cursor) {
        if (*cursor == 0) return;
        (*cursor)--;
        terminal_putc('\b');
    } else if (*cursor >= *length) {
        return;
    }

    for (uint32_t i = *cursor; i + 1 < *length; i++) {
        buffer[i] = buffer[i + 1];
    }
    (*length)--;
    for (uint32_t i = *cursor; i < *length; i++) {
        terminal_putc(buffer[i]);
    }
    terminal_putc(' ');
    for (uint32_t i = *cursor; i <= *length; i++) {
        terminal_putc('\b');
    }
}

static int input_inside_string(const char *buffer, uint32_t cursor)
{
    int in_string = 0;
    int escaped = 0;
    for (uint32_t i = 0; i < cursor; i++) {
        if (escaped) escaped = 0;
        else if (in_string && buffer[i] == '\\') escaped = 1;
        else if (buffer[i] == '"') in_string = !in_string;
    }
    return in_string;
}

static void complete_delimiters(char *buffer,
                                uint32_t capacity,
                                uint32_t *length,
                                uint32_t *cursor)
{
    int in_string = 0;
    int escaped = 0;
    int depth = 0;
    for (uint32_t i = 0; i < *length; i++) {
        char c = buffer[i];
        if (escaped) escaped = 0;
        else if (in_string && c == '\\') escaped = 1;
        else if (c == '"') in_string = !in_string;
        else if (!in_string && c == '(') depth++;
        else if (!in_string && c == ')' && depth > 0) depth--;
    }

    while (*cursor < *length) {
        terminal_putc(buffer[*cursor]);
        (*cursor)++;
    }
    if (in_string) {
        insert_input(buffer, capacity, length, cursor, "\"", 1, 1);
    }
    while (depth-- > 0) {
        if (!insert_input(buffer, capacity, length, cursor, ")", 1, 1)) {
            break;
        }
    }
}

static void remember_command(const char *buffer, uint32_t length)
{
    if (length == 0) return;

    if (command_history_count) {
        const char *previous = command_history[command_history_count - 1];
        uint32_t i = 0;
        while (i < length && previous[i] == buffer[i]) i++;
        if (i == length && previous[i] == '\0') return;
    }

    if (command_history_count == COMMAND_HISTORY_SIZE) {
        for (uint32_t i = 1; i < COMMAND_HISTORY_SIZE; i++) {
            for (uint32_t j = 0; j < COMMAND_HISTORY_LINE_SIZE; j++) {
                command_history[i - 1][j] = command_history[i][j];
            }
        }
        command_history_count--;
    }

    for (uint32_t i = 0; i < length; i++) {
        command_history[command_history_count][i] = buffer[i];
    }
    command_history[command_history_count][length] = '\0';
    command_history_count++;
}

static int input_is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int input_is_word_boundary(char c)
{
    return input_is_space(c) || c == '(' || c == ')' || c == '"';
}

static void move_word_left(const char *buffer, uint32_t *cursor)
{
    while (*cursor > 0 &&
           input_is_word_boundary(buffer[*cursor - 1])) {
        (*cursor)--;
    }
    while (*cursor > 0 &&
           !input_is_word_boundary(buffer[*cursor - 1])) {
        (*cursor)--;
    }
}

static void move_word_right(const char *buffer,
                            uint32_t length,
                            uint32_t *cursor)
{
    if (*cursor < length &&
        !input_is_word_boundary(buffer[*cursor])) {
        while (*cursor < length &&
               !input_is_word_boundary(buffer[*cursor])) {
            (*cursor)++;
        }
        return;
    }
    while (*cursor < length &&
           input_is_word_boundary(buffer[*cursor])) {
        (*cursor)++;
    }
    while (*cursor < length &&
           !input_is_word_boundary(buffer[*cursor])) {
        (*cursor)++;
    }
}

static void redraw_input_line(const char *prompt,
                              const char *buffer,
                              uint32_t length,
                              uint32_t cursor)
{
    terminal_clear();
    terminal_write(prompt);
    for (uint32_t i = 0; i < length; i++) {
        terminal_putc(buffer[i]);
    }
    for (uint32_t i = cursor; i < length; i++) {
        terminal_putc('\b');
    }
}

static int current_file_argument(const char *buffer, uint32_t token_start)
{
    uint32_t depth = 0;
    int in_string = 0;
    int escaped = 0;

    for (uint32_t i = 0; i < token_start; i++) {
        char c = buffer[i];
        if (in_string) {
            if (escaped) escaped = 0;
            else if (c == '\\') escaped = 1;
            else if (c == '"') in_string = 0;
            continue;
        }
        if (c == '"') {
            in_string = 1;
        } else if (c == '(' && depth < SYS_LINE_MAX) {
            input_open_positions[depth++] = (uint16_t)i;
        } else if (c == ')' && depth > 0) {
            depth--;
        }
    }
    if (depth == 0) return 0;

    uint32_t head = (uint32_t)input_open_positions[depth - 1] + 1;
    while (head < token_start && input_is_space(buffer[head])) head++;
    uint32_t head_end = head;
    while (head_end < token_start &&
           !input_is_space(buffer[head_end]) &&
           buffer[head_end] != '(' && buffer[head_end] != ')') {
        head_end++;
    }
    if (head_end - head != 4 ||
        buffer[head] != 'f' || buffer[head + 1] != 'i' ||
        buffer[head + 2] != 'l' || buffer[head + 3] != 'e') {
        return 0;
    }

    for (uint32_t i = head_end; i < token_start; i++) {
        if (input_is_space(buffer[i])) return 1;
    }
    return 0;
}

static uint32_t collect_text_matches(const char *source,
                                     size_t source_length,
                                     const char *prefix,
                                     size_t prefix_length)
{
    size_t used = 0;
    uint32_t count = 0;
    size_t offset = 0;
    while (offset < source_length) {
        size_t end = offset;
        while (end < source_length && source[end] != '\n') end++;
        size_t name_length = end - offset;
        int matches = name_length >= prefix_length;
        for (size_t i = 0; matches && i < prefix_length; i++) {
            if (source[offset + i] != prefix[i]) matches = 0;
        }
        if (matches && name_length &&
            used + name_length + 1 < sizeof(completion_matches)) {
            for (size_t i = 0; i < name_length; i++) {
                completion_matches[used++] = source[offset + i];
            }
            completion_matches[used++] = '\n';
            count++;
        }
        offset = end + 1;
    }
    completion_matches[used] = '\0';
    return count;
}

static int make_completion_directory(const char *buffer,
                                     uint32_t token_start,
                                     uint32_t cursor,
                                     char *directory,
                                     size_t capacity,
                                     uint32_t *leaf_start)
{
    uint32_t slash = cursor;
    for (uint32_t i = token_start; i < cursor; i++) {
        if (buffer[i] == '/') slash = i;
    }
    *leaf_start = slash == cursor ? token_start : slash + 1;

    size_t used = 0;
    if (buffer[token_start] == '/') {
        if (slash == cursor) {
            if (cursor - token_start >= capacity) return 0;
            for (uint32_t i = token_start; i < cursor; i++) {
                directory[used++] = buffer[i];
            }
            directory[used] = '\0';
            return 1;
        }
        if (slash == token_start) {
            directory[used++] = '/';
        } else {
            for (uint32_t i = token_start; i < slash; i++) {
                if (used + 1 >= capacity) return 0;
                directory[used++] = buffer[i];
            }
        }
    } else {
        const char *cwd = readline_completion.current_directory;
        while (cwd[used] && used + 1 < capacity) {
            directory[used] = cwd[used];
            used++;
        }
        if (cwd[used] || used == 0) return 0;
        if (slash != cursor) {
            if (used > 1 && directory[used - 1] != '/') {
                if (used + 1 >= capacity) return 0;
                directory[used++] = '/';
            }
            for (uint32_t i = token_start; i < slash; i++) {
                if (used + 1 >= capacity) return 0;
                directory[used++] = buffer[i];
            }
        }
    }
    if (used == 0) return 0;
    directory[used] = '\0';
    return 1;
}

static int append_path_match(const char *directory,
                             const char *name,
                             size_t name_length,
                             size_t *used)
{
    char candidate[SYS_PATH_MAX];
    size_t path_length = 0;
    while (directory[path_length] &&
           path_length + 1 < sizeof(candidate)) {
        candidate[path_length] = directory[path_length];
        path_length++;
    }
    if (directory[path_length]) return 0;
    if (candidate[path_length - 1] != '/') {
        if (path_length + 1 >= sizeof(candidate)) return 0;
        candidate[path_length++] = '/';
    }
    if (path_length + name_length >= sizeof(candidate)) return 0;
    for (size_t i = 0; i < name_length; i++) {
        candidate[path_length + i] = name[i];
    }
    candidate[path_length + name_length] = '\0';

    size_t probe_length = 0;
    int is_directory = ramfs_list(candidate, completion_probe,
                                  sizeof(completion_probe),
                                  &probe_length) == 0;
    size_t extra = is_directory ? 2 : 1;
    if (*used + name_length + extra >= sizeof(completion_matches)) return 0;
    for (size_t i = 0; i < name_length; i++) {
        completion_matches[(*used)++] = name[i];
    }
    if (is_directory) completion_matches[(*used)++] = '/';
    completion_matches[(*used)++] = '\n';
    return 1;
}

static uint32_t collect_path_matches(const char *directory,
                                     const char *prefix,
                                     size_t prefix_length)
{
    size_t listing_length = 0;
    if (ramfs_list(directory, filesystem_buffer,
                   sizeof(filesystem_buffer), &listing_length)) {
        return 0;
    }

    size_t used = 0;
    uint32_t count = 0;
    size_t offset = 0;
    while (offset < listing_length) {
        size_t end = offset;
        while (end < listing_length && filesystem_buffer[end] != '\n') end++;
        size_t name_length = end - offset;
        int matches = name_length >= prefix_length;
        for (size_t i = 0; matches && i < prefix_length; i++) {
            if (filesystem_buffer[offset + i] != prefix[i]) matches = 0;
        }
        if (matches && name_length &&
            append_path_match(directory, filesystem_buffer + offset,
                              name_length, &used)) {
            count++;
        }
        offset = end + 1;
    }
    completion_matches[used] = '\0';
    return count;
}

static void display_completion_matches(const char *prompt,
                                      const char *buffer,
                                      uint32_t length,
                                      uint32_t cursor)
{
    for (uint32_t i = cursor; i < length; i++) {
        terminal_putc(buffer[i]);
    }
    terminal_putc('\n');
    size_t offset = 0;
    while (completion_matches[offset]) {
        size_t end = offset;
        while (completion_matches[end] &&
               completion_matches[end] != '\n') {
            end++;
        }
        terminal_write("  ");
        for (size_t i = offset; i < end; i++) {
            terminal_putc(completion_matches[i]);
        }
        terminal_putc('\n');
        offset = end + 1;
    }
    terminal_write(prompt);
    for (uint32_t i = 0; i < length; i++) {
        terminal_putc(buffer[i]);
    }
    for (uint32_t i = cursor; i < length; i++) {
        terminal_putc('\b');
    }
}

static int complete_input(char *buffer,
                          uint32_t capacity,
                          uint32_t *length,
                          uint32_t *cursor)
{
    if (input_inside_string(buffer, *cursor)) return 0;

    uint32_t token_start = *cursor;
    while (token_start > 0 &&
           !input_is_space(buffer[token_start - 1]) &&
           buffer[token_start - 1] != '(' &&
           buffer[token_start - 1] != ')' &&
           buffer[token_start - 1] != '"') {
        token_start--;
    }
    uint32_t token_end = *cursor;
    while (token_end < *length &&
           !input_is_space(buffer[token_end]) &&
           buffer[token_end] != '(' &&
           buffer[token_end] != ')' &&
           buffer[token_end] != '"') {
        token_end++;
    }
    if (token_end != *cursor) return 0;

    uint32_t leaf_start = token_start;
    uint32_t count;
    char directory[SYS_PATH_MAX];
    if (current_file_argument(buffer, token_start) ||
        (token_start < *cursor &&
         (buffer[token_start] == '/' || buffer[token_start] == '.'))) {
        if (!make_completion_directory(buffer, token_start, *cursor,
                                       directory, sizeof(directory),
                                       &leaf_start)) {
            return 0;
        }
        count = collect_path_matches(directory, buffer + leaf_start,
                                     *cursor - leaf_start);
    } else {
        count = collect_text_matches(
            readline_completion.function_names,
            readline_completion.function_names_length,
            buffer + token_start,
            *cursor - token_start
        );
    }
    if (count == 0) return 0;

    size_t first_length = 0;
    while (completion_matches[first_length] &&
           completion_matches[first_length] != '\n') {
        first_length++;
    }
    size_t common_length = first_length;
    size_t offset = first_length + 1;
    while (offset < SYS_FILE_MAX && completion_matches[offset]) {
        size_t end = offset;
        while (completion_matches[end] &&
               completion_matches[end] != '\n') {
            end++;
        }
        size_t match_length = end - offset;
        if (match_length < common_length) common_length = match_length;
        size_t i = 0;
        while (i < common_length &&
               completion_matches[i] == completion_matches[offset + i]) {
            i++;
        }
        common_length = i;
        offset = end + 1;
    }

    uint32_t prefix_length = *cursor - leaf_start;
    if (common_length > prefix_length) {
        if (!insert_input(buffer, capacity, length, cursor,
                          completion_matches + prefix_length,
                          (uint32_t)(common_length - prefix_length),
                          (uint32_t)(common_length - prefix_length))) {
            return 0;
        }
    }
    if (count > 1) {
        display_completion_matches(readline_completion.prompt, buffer,
                                   *length, *cursor);
    }
    return 1;
}

int syscall_user_range_valid(uint32_t address,
                             uint32_t length,
                             int write)
{
    if (length == 0) return 1;
    if (address + length - 1 < address) return 0;

    uint32_t last = address + length - 1;
    uint32_t page = address & ~0xFFFu;
    for (;;) {
        if (write ? !vmm_check_user_writable(page) : !vmm_check(page, 0)) {
            return 0;
        }
        if (page == (last & ~0xFFFu)) return 1;
        page += 0x1000;
    }
}

static int bounded_string_length(const char *text,
                                 size_t capacity,
                                 size_t *length)
{
    for (size_t i = 0; i < capacity; i++) {
        if (text[i] == '\0') {
            *length = i;
            return 1;
        }
    }
    return 0;
}

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

char *syscall_filesystem_buffer(void)
{
    return filesystem_buffer;
}


uint32_t syscall_readline_handle(syscall_frame *frame)
{
    if (frame->ecx < 2 || frame->ecx > SYS_LINE_MAX ||
        !syscall_user_range_valid(frame->ebx, frame->ecx, 1)) {
        frame->eax = SYS_UNKNOWN_TEST;
        return 0;
    }

    int completion_enabled = 0;
    if (frame->edx || frame->esi) {
        size_t directory_length;
        size_t prompt_length;
        if (!frame->edx || frame->esi != sizeof(readline_completion) ||
            !syscall_user_range_valid(frame->edx, frame->esi, 0)) {
            frame->eax = SYS_UNKNOWN_TEST;
            return 0;
        }
        const uint8_t *source = (const uint8_t *)frame->edx;
        uint8_t *destination = (uint8_t *)&readline_completion;
        for (size_t i = 0; i < sizeof(readline_completion); i++) {
            destination[i] = source[i];
        }
        if (readline_completion.function_names_length >=
                sizeof(readline_completion.function_names) ||
            readline_completion.current_directory[0] != '/' ||
            !bounded_string_length(
                readline_completion.current_directory,
                sizeof(readline_completion.current_directory),
                &directory_length
            ) ||
            !bounded_string_length(
                readline_completion.prompt,
                sizeof(readline_completion.prompt),
                &prompt_length
            ) ||
            (readline_completion.function_names_length &&
             readline_completion.function_names[
                 readline_completion.function_names_length - 1
             ] != '\n') ||
            readline_completion.function_names[
                readline_completion.function_names_length
            ] != '\0') {
            frame->eax = SYS_UNKNOWN_TEST;
            return 0;
        }
        for (size_t i = 0;
             i < readline_completion.function_names_length;
             i++) {
            uint8_t candidate =
                (uint8_t)readline_completion.function_names[i];
            if (candidate == '\0' ||
                (candidate < 32 && candidate != '\n')) {
                frame->eax = SYS_UNKNOWN_TEST;
                return 0;
            }
        }
        (void)directory_length;
        (void)prompt_length;
        completion_enabled = 1;
    }

    char *buffer = (char *)frame->ebx;
    uint32_t length = 0;
    uint32_t cursor = 0;
    uint32_t history_cursor = command_history_count;
    command_history_draft_valid = 0;
    buffer[0] = '\0';
    for (;;) {
        terminal_cursor_show();
        int c = terminal_getchar();
        terminal_cursor_hide();
        if (c == '\n') {
            complete_delimiters(
                buffer, frame->ecx, &length, &cursor
            );
            terminal_putc('\n');
            buffer[length] = '\0';
            remember_command(buffer, length);
            command_history_draft_valid = 0;
            frame->eax = length;
            return 0;
        }
        if (c == TERMINAL_KEY_UP) {
            if (history_cursor > 0) {
                if (history_cursor == command_history_count) {
                    for (uint32_t i = 0; i <= length; i++) {
                        command_history_draft[i] = buffer[i];
                    }
                    command_history_draft_valid = 1;
                }
                history_cursor--;
                replace_input_line(
                    buffer,
                    frame->ecx,
                    &length,
                    &cursor,
                    command_history[history_cursor]
                );
            }
            continue;
        }
        if (c == TERMINAL_KEY_DOWN) {
            if (history_cursor < command_history_count) {
                history_cursor++;
                replace_input_line(
                    buffer,
                    frame->ecx,
                    &length,
                    &cursor,
                    history_cursor == command_history_count
                        ? (command_history_draft_valid
                            ? command_history_draft
                            : "")
                        : command_history[history_cursor]
                );
            }
            continue;
        }
        if (c == TERMINAL_KEY_CTRL_LEFT) {
            uint32_t previous = cursor;
            move_word_left(buffer, &cursor);
            while (previous > cursor) {
                terminal_putc('\b');
                previous--;
            }
            continue;
        }
        if (c == TERMINAL_KEY_CTRL_RIGHT) {
            uint32_t previous = cursor;
            move_word_right(buffer, length, &cursor);
            while (previous < cursor) {
                terminal_putc(buffer[previous]);
                previous++;
            }
            continue;
        }
        if (c == TERMINAL_KEY_LEFT) {
            if (cursor > 0) {
                cursor--;
                terminal_putc('\b');
            }
            continue;
        }
        if (c == TERMINAL_KEY_RIGHT) {
            if (cursor < length) {
                terminal_putc(buffer[cursor]);
                cursor++;
            }
            continue;
        }
        if (c == SYS_KEY_CTRL_A || c == TERMINAL_KEY_HOME) {
            while (cursor > 0) {
                cursor--;
                terminal_putc('\b');
            }
            continue;
        }
        if (c == SYS_KEY_CTRL_E || c == TERMINAL_KEY_END) {
            while (cursor < length) {
                terminal_putc(buffer[cursor]);
                cursor++;
            }
            continue;
        }
        if (c == SYS_KEY_CTRL_U) {
            uint32_t previous_length = length;
            while (cursor > 0) {
                delete_input(buffer, &length, &cursor, 1);
            }
            if (length != previous_length) {
                history_cursor = command_history_count;
                command_history_draft_valid = 0;
            }
            continue;
        }
        if (c == SYS_KEY_CTRL_K) {
            uint32_t previous_length = length;
            while (cursor < length) {
                delete_input(buffer, &length, &cursor, 0);
            }
            if (length != previous_length) {
                history_cursor = command_history_count;
                command_history_draft_valid = 0;
            }
            continue;
        }
        if (c == SYS_KEY_CTRL_W) {
            uint32_t previous_length = length;
            while (cursor > 0 &&
                   input_is_word_boundary(buffer[cursor - 1])) {
                delete_input(buffer, &length, &cursor, 1);
            }
            while (cursor > 0 &&
                   !input_is_word_boundary(buffer[cursor - 1])) {
                delete_input(buffer, &length, &cursor, 1);
            }
            if (length != previous_length) {
                history_cursor = command_history_count;
                command_history_draft_valid = 0;
            }
            continue;
        }
        if (c == SYS_KEY_CTRL_L) {
            redraw_input_line(
                completion_enabled ? readline_completion.prompt : "",
                buffer,
                length,
                cursor
            );
            continue;
        }
        if (c == '\b') {
            uint32_t previous_length = length;
            delete_input(buffer, &length, &cursor, 1);
            if (length != previous_length) {
                history_cursor = command_history_count;
                command_history_draft_valid = 0;
            }
            continue;
        }
        if (c == TERMINAL_KEY_DELETE) {
            uint32_t previous_length = length;
            delete_input(buffer, &length, &cursor, 0);
            if (length != previous_length) {
                history_cursor = command_history_count;
                command_history_draft_valid = 0;
            }
            continue;
        }
        if (c == '\t') {
            uint32_t previous_length = length;
            if (completion_enabled) {
                (void)complete_input(buffer, frame->ecx,
                                     &length, &cursor);
            }
            if (length != previous_length) {
                history_cursor = command_history_count;
                command_history_draft_valid = 0;
            }
            continue;
        }
        if (c < 0 || c > 0xFF) continue;
        uint8_t input = (uint8_t)c;
        int in_string = input_inside_string(buffer, cursor);
        if ((input == ')' && !in_string && cursor < length &&
             buffer[cursor] == ')') ||
            (input == '"' && cursor < length &&
             buffer[cursor] == '"')) {
            terminal_putc(buffer[cursor]);
            cursor++;
            continue;
        }
        if (input < 32 || input == 0x7F) continue;
        if (input == '(' && !in_string) {
            if (!insert_input(
                    buffer, frame->ecx, &length, &cursor, "()", 2, 1)) {
                char open = '(';
                insert_input(
                    buffer, frame->ecx, &length, &cursor, &open, 1, 1
                );
            }
            history_cursor = command_history_count;
            command_history_draft_valid = 0;
            continue;
        }
        if (input == '"' && !in_string) {
            if (!insert_input(
                    buffer, frame->ecx, &length, &cursor, "\"\"", 2, 1)) {
                char quote = '"';
                insert_input(
                    buffer, frame->ecx, &length, &cursor, &quote, 1, 1
                );
            }
            history_cursor = command_history_count;
            command_history_draft_valid = 0;
            continue;
        }
        char text[1] = { (char)input };
        if (insert_input(
                buffer, frame->ecx, &length, &cursor, text, 1, 1)) {
            history_cursor = command_history_count;
            command_history_draft_valid = 0;
        }
    }
}

uint32_t syscall_dispatch(syscall_frame *frame)
{
    const syscall_handler_entry *entry;
    for (entry = __start_syscall_handlers;
         entry < __stop_syscall_handlers;
         entry++) {
        if (entry->number == frame->eax) {
            return entry->handler(frame);
        }
    }

    frame->eax = SYS_UNKNOWN_TEST;
    return 0;
}

lucid_status syscall_pre(void)
{
    return LUCID_OK;
}

lucid_status syscall_init(void)
{
    idt_install_user_handler(0x80, (uint32_t)syscall_isr);
    return LUCID_OK_MSG("int 0x80 syscall ABI installed");
}

lucid_status syscall_post(void)
{
    return LUCID_OK;
}
