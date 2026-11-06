#include "clearsh_internal.h"

static unsigned int script_depth;

static value run_script_body(const value *file)
{
    char source[TEXT_MAX];
    size_t length = 0;
    if (ramfs_read(file->data.text, source, sizeof(source) - 1, &length)) {
        value error = make_error("script could not be read");
        print_value(&error);
        terminal_putc('\n');
        return error;
    }
    source[length] = '\0';

    char line[LINE_MAX];
    size_t used = 0;
    for (size_t i = 0; i <= length; i++) {
        if (source[i] != '\n' && source[i] != '\0') {
            if (used + 1 >= sizeof(line)) return make_error("script line is too long");
            line[used++] = source[i];
            continue;
        }
        line[used] = '\0';
        used = 0;
        if (line[0] == '#' && line[1] == '!') continue;
        arena_used = 0;
        value result = evaluate_line(line);
        if (result.type == VALUE_ERROR) {
            print_value(&result);
            terminal_putc('\n');
            return result;
        }
        if (result.type != VALUE_NIL) {
            print_value(&result);
            terminal_putc('\n');
        }
        if (script_pending) {
            value nested_result = run_pending_script();
            if (nested_result.type == VALUE_ERROR) return nested_result;
        }
    }
    return make_value(VALUE_NIL);
}

static value run_script(const value *file)
{
    if (script_depth >= MAX_DEPTH) {
        value error = make_error("script nesting is too deep");
        print_value(&error);
        terminal_putc('\n');
        return error;
    }

    script_depth++;
    value result = run_script_body(file);
    script_depth--;
    return result;
}

value run_pending_script(void)
{
    value file = make_value(VALUE_FILE);
    string_copy(file.data.text, pending_script_path,
                sizeof(file.data.text));
    script_pending = 0;
    return run_script(&file);
}

static void run_pioneer_scripts(void)
{
    char source[TEXT_MAX];
    size_t length = 0;
    if (ramfs_read("/system/config/pioneer/boot",
                   source, sizeof(source) - 1, &length)) {
        terminal_write("Pioneer: no boot script list; starting shell.\n");
        return;
    }
    source[length] = '\0';

    size_t offset = 0;
    while (offset < length) {
        char entry[PATH_MAX];
        size_t used = 0;
        int too_long = 0;
        while (offset < length && source[offset] != '\n') {
            if (used + 1 < sizeof(entry)) {
                entry[used++] = source[offset];
            } else {
                too_long = 1;
            }
            offset++;
        }
        if (offset < length && source[offset] == '\n') offset++;
        entry[used] = '\0';
        if (too_long) {
            terminal_write("Pioneer: boot entry is too long; skipped.\n");
            continue;
        }

        size_t start = 0;
        while (entry[start] == ' ' || entry[start] == '\t') start++;
        size_t end = string_length(entry);
        while (end > start &&
               (entry[end - 1] == ' ' || entry[end - 1] == '\t' ||
                entry[end - 1] == '\r')) {
            entry[--end] = '\0';
        }
        if (start == end || entry[start] == '#') continue;

        char configured_path[PATH_MAX];
        if (entry[start] == '/') {
            string_copy(configured_path, entry + start,
                        sizeof(configured_path));
        } else {
            static const char prefix[] = "/system/config/pioneer/";
            size_t prefix_length = sizeof(prefix) - 1;
            if (prefix_length + end - start >= sizeof(configured_path)) {
                terminal_write("Pioneer: boot path is too long; skipped.\n");
                continue;
            }
            for (size_t i = 0; i < prefix_length; i++) {
                configured_path[i] = prefix[i];
            }
            for (size_t i = start; i < end; i++) {
                configured_path[prefix_length + i - start] = entry[i];
            }
            configured_path[prefix_length + end - start] = '\0';
        }

        char resolved[PATH_MAX];
        if (!resolve_path(configured_path, resolved, sizeof(resolved))) {
            terminal_write("Pioneer: invalid boot path; skipped.\n");
            continue;
        }
        value file = make_value(VALUE_FILE);
        string_copy(file.data.text, resolved, sizeof(file.data.text));
        terminal_write("Pioneer: running ");
        terminal_write(resolved);
        terminal_putc('\n');
        arena_used = 0;
        (void)run_script(&file);
    }
}

static void read_line(char *buffer, size_t capacity)
{
    sys_readline_completion completion;
    uint8_t *completion_bytes = (uint8_t *)&completion;
    for (size_t i = 0; i < sizeof(completion); i++) {
        completion_bytes[i] = 0;
    }

    size_t function_names_length = 0;
    size_t directory_length = string_length(current_directory);
    static const char prompt_prefix[] = "\xCE\xBB ";
    static const char prompt_suffix[] = "> ";
    size_t prompt_prefix_length = sizeof(prompt_prefix) - 1;
    size_t prompt_suffix_length = sizeof(prompt_suffix) - 1;
    if (directory_length >= sizeof(completion.current_directory) ||
        prompt_prefix_length + directory_length + prompt_suffix_length >=
            sizeof(completion.prompt) ||
        !clearsh_function_candidates(
            completion.function_names,
            sizeof(completion.function_names),
            &function_names_length
        )) {
        terminal_write("Clearsh: could not prepare line completion.\n");
        buffer[0] = '\0';
        return;
    }

    for (size_t i = 0; i < directory_length; i++) {
        completion.current_directory[i] = current_directory[i];
    }
    for (size_t i = 0; i < prompt_prefix_length; i++) {
        completion.prompt[i] = prompt_prefix[i];
    }
    for (size_t i = 0; i < directory_length; i++) {
        completion.prompt[prompt_prefix_length + i] = current_directory[i];
    }
    for (size_t i = 0; i < prompt_suffix_length; i++) {
        completion.prompt[prompt_prefix_length + directory_length + i] =
            prompt_suffix[i];
    }
    completion.function_names_length = (uint32_t)function_names_length;

    uint32_t length = clearsh_syscall(
        SYS_READLINE,
        (uint32_t)buffer,
        capacity,
        (uint32_t)&completion,
        sizeof(completion)
    );
    if (length == SYS_UNKNOWN_TEST) {
        terminal_write("Clearsh: line input failed.\n");
        buffer[0] = '\0';
        return;
    }
    if (length >= capacity) {
        terminal_write("Clearsh: line input exceeded its buffer.\n");
        buffer[0] = '\0';
        return;
    }
    buffer[length] = '\0';
}

static void shell_loop(void)
{
    char line[LINE_MAX];

    for (;;) {
        terminal_write("\xCE\xBB ");
        terminal_write(current_directory);
        terminal_write("> ");
        read_line(line, sizeof(line));
        arena_used = 0;
        value result = evaluate_line(line);
        if (result.type == VALUE_ERROR) {
            print_value(&result);
            terminal_putc('\n');
        } else if (result.type != VALUE_NIL) {
            print_value(&result);
            terminal_putc('\n');
        }
        if (script_pending) (void)run_pending_script();
    }
}

__attribute__((section(".entry")))
void clearsh_user_entry(void)
{
    terminal_write("\nClearsh Lisp shell\n");
    run_pioneer_scripts();
    shell_loop();
}
