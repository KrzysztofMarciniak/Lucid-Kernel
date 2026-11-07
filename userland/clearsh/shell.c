#include "clearsh_internal.h"

static unsigned int script_depth;

#ifdef LUCID_TEST_MODE
static void test_unknown_command_errors(void)
{
    value result = evaluate_line("(pritn)");
    if (result.type != VALUE_ERROR ||
        !string_equal(result.data.text,
                      "unknown command 'pritn'; did you mean 'print'?")) {
        for (;;) {
            __asm__ volatile ("pause");
        }
    }

    result = evaluate_line("(zzzzzzzzzz)");
    if (result.type != VALUE_ERROR ||
        !string_equal(result.data.text, "unknown command 'zzzzzzzzzz'")) {
        for (;;) {
            __asm__ volatile ("pause");
        }
    }
}
#endif

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
        arena_used = 0;
        (void)run_script(&file);
    }
}

static int append_prompt_text(char *buffer,
                              size_t capacity,
                              size_t *length,
                              const char *text,
                              size_t text_length)
{
    if (text_length >= capacity - *length) return 0;
    for (size_t i = 0; i < text_length; i++) {
        buffer[(*length)++] = text[i];
    }
    return 1;
}

static size_t build_prompt(char *buffer, size_t capacity)
{
    static const char path[] = "/system/config/clearsh/prompt";
    static const char default_template[] = "\xCE\xBB %p> ";
    static int config_error_reported;
    char source[128];
    uint32_t length = clearsh_syscall(
        SYS_FS_READ,
        (uint32_t)path,
        sizeof(path) - 1,
        (uint32_t)source,
        sizeof(source)
    );
    int valid = length != SYS_UNKNOWN_TEST && length < sizeof(source);
    if (valid) {
        while (length &&
               (source[length - 1] == '\n' || source[length - 1] == '\r')) {
            length--;
        }
    }
    if (!valid) {
        length = sizeof(default_template) - 1;
        for (size_t i = 0; i < length; i++) source[i] = default_template[i];
    }

    size_t output_length = 0;
    for (size_t i = 0; i < length && valid; i++) {
        char character = source[i];
        if ((unsigned char)character < 0x20 || character == 0x7F) {
            valid = 0;
            break;
        }
        if (character != '%') {
            if (!append_prompt_text(buffer, capacity, &output_length,
                                    &character, 1)) {
                valid = 0;
                break;
            }
            continue;
        }
        if (++i >= length) {
            valid = 0;
            break;
        }
        if (source[i] == 'p') {
            size_t directory_length = string_length(current_directory);
            if (!append_prompt_text(buffer, capacity, &output_length,
                                    current_directory,
                                    directory_length)) {
                valid = 0;
                break;
            }
        } else if (source[i] == '%') {
            if (!append_prompt_text(buffer, capacity, &output_length,
                                    "%", 1)) {
                valid = 0;
                break;
            }
        } else {
            valid = 0;
        }
    }

    if (!valid) {
        if (!config_error_reported) {
            terminal_write(
                "Clearsh: invalid prompt config; using the default\n"
            );
            config_error_reported = 1;
        }
        source[0] = '\0';
        output_length = 0;
        for (size_t i = 0; i < sizeof(default_template) - 1; i++) {
            char character = default_template[i];
            if (character == '%' && default_template[i + 1] == 'p') {
                size_t directory_length = string_length(current_directory);
                if (!append_prompt_text(buffer, capacity, &output_length,
                                        current_directory,
                                        directory_length)) {
                    buffer[0] = '\0';
                    return 0;
                }
                i++;
            } else if (!append_prompt_text(buffer, capacity, &output_length,
                                           &character, 1)) {
                buffer[0] = '\0';
                return 0;
            }
        }
    } else {
        config_error_reported = 0;
    }
    buffer[output_length] = '\0';
    return output_length;
}

static void read_line(char *buffer,
                      size_t capacity,
                      const char *prompt,
                      size_t prompt_length)
{
    sys_readline_completion completion;
    uint8_t *completion_bytes = (uint8_t *)&completion;
    for (size_t i = 0; i < sizeof(completion); i++) {
        completion_bytes[i] = 0;
    }

    size_t function_names_length = 0;
    size_t directory_length = string_length(current_directory);
    if (directory_length >= sizeof(completion.current_directory) ||
        prompt_length >= sizeof(completion.prompt) ||
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
    for (size_t i = 0; i < prompt_length; i++) {
        completion.prompt[i] = prompt[i];
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
        char prompt[SYS_PROMPT_MAX];
        size_t prompt_length = build_prompt(prompt, sizeof(prompt));
        terminal_write(prompt);
        read_line(line, sizeof(line), prompt, prompt_length);
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
#ifdef LUCID_TEST_MODE
    test_unknown_command_errors();
#endif
    run_pioneer_scripts();
    shell_loop();
}
