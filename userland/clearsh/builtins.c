#include "clearsh_internal.h"

static value apply_function(function_id id, const value *arg)
{
    if (id == FUNCTION_TEXT) {
        value result = make_value(VALUE_TEXT);
        if (!value_to_text(arg, result.data.text, sizeof(result.data.text))) {
            return make_error("value cannot be converted to text");
        }
        return result;
    }
    if (id == FUNCTION_FILE) {
        char path[PATH_MAX];
        char resolved[PATH_MAX];
        if (!value_to_text(arg, path, sizeof(path)) ||
            !resolve_path(path, resolved, sizeof(resolved))) {
            return make_error("file path is invalid or too long");
        }
        value result = make_value(VALUE_FILE);
        string_copy(result.data.text, resolved, sizeof(result.data.text));
        return result;
    }
    return make_error("function cannot be used as a list operation");
}

static value call_math(function_id id, const value *args, size_t count)
{
    int32_t accumulator;
    int32_t next;
    if (id == FUNCTION_ADD || id == FUNCTION_SUBTRACT ||
        id == FUNCTION_MULTIPLY || id == FUNCTION_DIVIDE) {
        size_t first = 0;
        if (count == 0 ||
            (id != FUNCTION_SUBTRACT && id != FUNCTION_ADD && count < 2)) {
            return make_error("arithmetic function has too few arguments");
        }
        if (!get_number(&args[0], &accumulator)) {
            return make_error("arithmetic expects numbers");
        }
        if (id == FUNCTION_ADD) accumulator = 0;
        else if (id == FUNCTION_MULTIPLY) accumulator = 1;
        else if (id == FUNCTION_SUBTRACT && count == 1) {
            if (!checked_number(-(int64_t)accumulator, &next)) {
                return make_error("integer overflow");
            }
            return make_number(next);
        } else {
            first = 1;
        }

        for (size_t i = first; i < count; i++) {
            if (!get_number(&args[i], &next)) {
                return make_error("arithmetic expects numbers");
            }
            int64_t result;
            if (id == FUNCTION_ADD) result = (int64_t)accumulator + next;
            else if (id == FUNCTION_SUBTRACT) {
                result = (int64_t)accumulator - next;
            } else if (id == FUNCTION_MULTIPLY) {
                result = (int64_t)accumulator * next;
            } else {
                if (next == 0) return make_error("division by zero");
                if (accumulator == (-2147483647 - 1) && next == -1) {
                    return make_error("integer overflow");
                }
                result = accumulator / next;
            }
            if (!checked_number(result, &accumulator)) {
                return make_error("integer overflow");
            }
        }
        return make_number(accumulator);
    }

    if (id == FUNCTION_MODULO) {
        if (count != 2 || !get_number(&args[0], &accumulator) ||
            !get_number(&args[1], &next)) {
            return make_error("% expects two numbers");
        }
        if (next == 0) return make_error("modulo by zero");
        if (accumulator == (-2147483647 - 1) && next == -1) {
            return make_number(0);
        }
        return make_number(accumulator % next);
    }

    if (id == FUNCTION_ABS) {
        if (count != 1 || !get_number(&args[0], &accumulator)) {
            return make_error("abs expects one number");
        }
        if (accumulator == (-2147483647 - 1)) {
            return make_error("integer overflow");
        }
        return make_number(accumulator < 0 ? -accumulator : accumulator);
    }

    if (id == FUNCTION_MIN || id == FUNCTION_MAX) {
        if (count == 0 || !get_number(&args[0], &accumulator)) {
            return make_error("min/max expects one or more numbers");
        }
        for (size_t i = 1; i < count; i++) {
            if (!get_number(&args[i], &next)) {
                return make_error("min/max expects numbers");
            }
            if ((id == FUNCTION_MIN && next < accumulator) ||
                (id == FUNCTION_MAX && next > accumulator)) {
                accumulator = next;
            }
        }
        return make_number(accumulator);
    }

    if (id == FUNCTION_POWER) {
        if (count != 2 || !get_number(&args[0], &accumulator) ||
            !get_number(&args[1], &next)) {
            return make_error("pow expects two numbers");
        }
        if (next < 0 || next > 31) {
            return make_error("pow exponent must be between 0 and 31");
        }
        int32_t result = 1;
        for (int32_t i = 0; i < next; i++) {
            if (!checked_number((int64_t)result * accumulator, &result)) {
                return make_error("integer overflow");
            }
        }
        return make_number(result);
    }

    if (id == FUNCTION_EQUAL) {
        if (count != 2) return make_error("= expects two values");
        value result = make_value(VALUE_BOOLEAN);
        result.data.boolean = equal_values(&args[0], &args[1]);
        return result;
    }

    if (id == FUNCTION_LESS || id == FUNCTION_LESS_EQUAL ||
        id == FUNCTION_GREATER || id == FUNCTION_GREATER_EQUAL) {
        if (count != 2 || !get_number(&args[0], &accumulator) ||
            !get_number(&args[1], &next)) {
            return make_error("comparison expects two numbers");
        }
        value result = make_value(VALUE_BOOLEAN);
        if (id == FUNCTION_LESS) result.data.boolean = accumulator < next;
        else if (id == FUNCTION_LESS_EQUAL) {
            result.data.boolean = accumulator <= next;
        } else if (id == FUNCTION_GREATER) {
            result.data.boolean = accumulator > next;
        } else {
            result.data.boolean = accumulator >= next;
        }
        return result;
    }

    if (id == FUNCTION_NOT) {
        if (count != 1) return make_error("not expects one value");
        value result = make_value(VALUE_BOOLEAN);
        result.data.boolean = !truthy(&args[0]);
        return result;
    }

    if (id == FUNCTION_AND || id == FUNCTION_OR) {
        if (count == 0) return make_error("and/or expects at least one value");
        value result = make_value(VALUE_BOOLEAN);
        result.data.boolean = id == FUNCTION_AND;
        for (size_t i = 0; i < count; i++) {
            if (id == FUNCTION_AND && !truthy(&args[i])) {
                result.data.boolean = 0;
                break;
            }
            if (id == FUNCTION_OR && truthy(&args[i])) {
                result.data.boolean = 1;
                break;
            }
        }
        return result;
    }

    return make_error("unknown math function");
}

value call_function(function_id id, value *args, size_t count)
{
    if (id == FUNCTION_SYSCALL) {
        int32_t number;
        if (count == 0 || !get_number(&args[0], &number) || number < 0) {
            return make_error("syscall expects a nonnegative syscall number");
        }

        uint32_t result;
        switch ((uint32_t)number) {
        case SYS_WRITE:
            if (count != 2 || args[1].type != VALUE_TEXT) {
                return make_error("syscall 0 expects one text value");
            }
            result = clearsh_syscall(
                SYS_WRITE,
                (uint32_t)args[1].data.text,
                string_length(args[1].data.text),
                0,
                0
            );
            if (result == SYS_UNKNOWN_TEST) {
                return make_error("syscall 0 failed");
            }
            return make_number((int32_t)result);

        case SYS_READLINE: {
            char line[LINE_MAX];
            if (count != 1) return make_error("syscall 2 takes no arguments");
            result = clearsh_syscall(
                SYS_READLINE, (uint32_t)line, sizeof(line), 0, 0
            );
            if (result == SYS_UNKNOWN_TEST || result >= sizeof(line)) {
                return make_error("syscall 2 failed");
            }
            value output = make_value(VALUE_TEXT);
            for (size_t i = 0; i < result; i++) {
                output.data.text[i] = line[i];
            }
            output.data.text[result] = '\0';
            return output;
        }

        case SYS_FS_READ: {
            char contents[TEXT_MAX];
            size_t length = 0;
            if (count != 2 || args[1].type != VALUE_FILE) {
                return make_error("syscall 3 expects one file value");
            }
            if (ramfs_read(args[1].data.text, contents,
                           sizeof(contents) - 1, &length)) {
                return make_error("syscall 3 failed");
            }
            contents[length] = '\0';
            value output = make_value(VALUE_TEXT);
            string_copy(output.data.text, contents,
                        sizeof(output.data.text));
            return output;
        }

        case SYS_FS_WRITE:
            if (count != 3 || args[1].type != VALUE_FILE ||
                args[2].type != VALUE_TEXT) {
                return make_error("syscall 4 expects a file and text value");
            }
            if (ramfs_write(args[1].data.text, args[2].data.text,
                            string_length(args[2].data.text))) {
                return make_error("syscall 4 failed");
            }
            return make_number((int32_t)string_length(args[2].data.text));

        case SYS_FS_LIST: {
            char entries[TEXT_MAX];
            size_t length = 0;
            if (count != 2 || args[1].type != VALUE_FILE) {
                return make_error("syscall 5 expects one directory value");
            }
            if (ramfs_list(args[1].data.text, entries,
                           sizeof(entries) - 1, &length)) {
                return make_error("syscall 5 failed");
            }
            entries[length] = '\0';
            value output = make_value(VALUE_TEXT);
            string_copy(output.data.text, entries,
                        sizeof(output.data.text));
            return output;
        }

        case SYS_READKEY:
            if (count != 1) return make_error("syscall 6 takes no arguments");
            return make_number((int32_t)clearsh_syscall(
                SYS_READKEY, 0, 0, 0, 0
            ));

        case SYS_CLEAR:
            if (count != 1) return make_error("syscall 7 takes no arguments");
            return make_number((int32_t)clearsh_syscall(
                SYS_CLEAR, 0, 0, 0, 0
            ));

        case SYS_SHUTDOWN:
            if (count != 1) return make_error("syscall 8 takes no arguments");
            clearsh_syscall(SYS_SHUTDOWN, 0, 0, 0, 0);
            return make_error("shutdown request returned unexpectedly");

        case SYS_GETTICKS:
            if (count != 1) return make_error("syscall 9 takes no arguments");
            result = clearsh_syscall(SYS_GETTICKS, 0, 0, 0, 0);
            if (result > 2147483647u) {
                return make_error("syscall result exceeds integer range");
            }
            return make_number((int32_t)result);

        case SYS_GET_TERMINAL_SIZE:
            if (count != 1) {
                return make_error("syscall 14 takes no arguments");
            }
            return make_number((int32_t)clearsh_syscall(
                SYS_GET_TERMINAL_SIZE, 0, 0, 0, 0
            ));

        case SYS_EXIT:
            return make_error("syscall 1 is unavailable while Clearsh is running");

        default:
            return make_error("unsupported syscall number");
        }
    }

    if (id == FUNCTION_UPTIME) {
        if (count != 0) return make_error("uptime takes no arguments");
        uint32_t ticks = clearsh_syscall(SYS_GETTICKS, 0, 0, 0, 0);
        return make_number((int32_t)(ticks / 100));
    }

    if (id >= FUNCTION_ADD && id <= FUNCTION_OR) {
        return call_math(id, args, count);
    }

    if (id == FUNCTION_LENGTH) {
        if (count != 1) return make_error("length expects one value");
        if (args[0].type == VALUE_TEXT) {
            return make_number((int32_t)string_length(args[0].data.text));
        }
        if (args[0].type == VALUE_LIST) {
            return make_number((int32_t)args[0].data.list.count);
        }
        return make_error("length expects text or a list");
    }

    if (id == FUNCTION_CONCAT) {
        value result = make_value(VALUE_TEXT);
        size_t used = 0;
        if (count < 2) return make_error("concat expects at least two values");
        for (size_t i = 0; i < count; i++) {
            char text[TEXT_MAX];
            if (!value_to_text(&args[i], text, sizeof(text))) {
                return make_error("concat value cannot be converted to text");
            }
            size_t length = string_length(text);
            if (used + length >= sizeof(result.data.text)) {
                return make_error("concatenated text is too long");
            }
            for (size_t j = 0; j < length; j++) {
                result.data.text[used++] = text[j];
            }
        }
        result.data.text[used] = '\0';
        return result;
    }

    if (id == FUNCTION_LIST) {
        if (arena_used + count > ARENA_SIZE) return make_error("list storage exhausted");
        value result = make_value(VALUE_LIST);
        result.data.list.items = &arena[arena_used];
        result.data.list.count = count;
        for (size_t i = 0; i < count; i++) {
            arena[arena_used++] = args[i];
        }
        return result;
    }

    if (id == FUNCTION_PRINT) {
        if (count != 1) return make_error("print expects one value");
        print_value(&args[0]);
        if (args[0].type != VALUE_TEXT ||
            args[0].data.text[string_length(args[0].data.text) ?
                string_length(args[0].data.text) - 1 : 0] != '\n') {
            terminal_putc('\n');
        }
        return make_value(VALUE_NIL);
    }

    if (id == FUNCTION_TEXT) {
        if (count != 1) return make_error("text expects one value");
        return apply_function(FUNCTION_TEXT, &args[0]);
    }

    if (id == FUNCTION_FILE) {
        if (count != 1) return make_error("file expects one path");
        return apply_function(FUNCTION_FILE, &args[0]);
    }

    if (id == FUNCTION_READ) {
        char buffer[TEXT_MAX];
        size_t length = 0;
        if (count != 1 || args[0].type != VALUE_FILE) {
            return make_error("read expects a file value");
        }
        if (ramfs_read(args[0].data.text, buffer, sizeof(buffer) - 1, &length)) {
            return make_error("file could not be read");
        }
        value result = make_value(VALUE_TEXT);
        for (size_t i = 0; i < length; i++) result.data.text[i] = buffer[i];
        result.data.text[length] = '\0';
        return result;
    }

    if (id == FUNCTION_WRITE) {
        char text[TEXT_MAX];
        if (count != 2 || args[0].type != VALUE_FILE) {
            return make_error("write expects a file and text value");
        }
        if (!value_to_text(&args[1], text, sizeof(text))) {
            return make_error("write value cannot be converted to text");
        }
        if (ramfs_write(args[0].data.text, text, string_length(text))) {
            return make_error("file could not be written");
        }
        return make_value(VALUE_NIL);
    }

    if (id == FUNCTION_MAP || id == FUNCTION_FILTER) {
        if (count != 2 ||
            (args[0].type != VALUE_FUNCTION &&
             args[0].type != VALUE_LAMBDA) ||
            args[1].type != VALUE_LIST) {
            return make_error("map/filter expects a function and list");
        }
        size_t count_items = args[1].data.list.count;
        if (arena_used + count_items > ARENA_SIZE) {
            return make_error("list storage exhausted");
        }
        value result = make_value(VALUE_LIST);
        result.data.list.items = &arena[arena_used];
        result.data.list.count = 0;
        for (size_t i = 0; i < count_items; i++) {
            value item = args[1].data.list.items[i];
            value mapped = args[0].type == VALUE_LAMBDA
                ? evaluate_lambda(&args[0], &item)
                : apply_function(args[0].data.function, &item);
            if (mapped.type == VALUE_ERROR) return mapped;
            if (id == FUNCTION_MAP || truthy(&mapped)) {
                arena[arena_used++] = id == FUNCTION_MAP ? mapped : item;
                result.data.list.count++;
            }
        }
        return result;
    }

    if (id == FUNCTION_RUN) {
        if (count != 1 || args[0].type != VALUE_FILE) {
            return make_error("run expects a file value");
        }
        if (script_pending) {
            return make_error("a script is already queued");
        }
        string_copy(pending_script_path, args[0].data.text,
                    sizeof(pending_script_path));
        script_pending = 1;
        return make_value(VALUE_NIL);
    }

    if (id == FUNCTION_HELP) {
        if (count != 0) return make_error("help takes no arguments");
        terminal_write("Clearsh: one Lisp expression per line: (function argument ...)\n");
        terminal_write("Files: (read (file path)), (write (file path) (text value))\n");
        terminal_write("Dirs: (ls), (ls (file /path)), (cd (file /path)), (pwd)\n");
        terminal_write("Try: (print (read (file /system/info/name)))\n");
        terminal_write("Text: text concat length print; run: (run (file /users/root/start.clearsh))\n");
        terminal_write("Math: + - * / % abs min max pow; compare: = < <= > >=\n");
        terminal_write("Logic: not and or if; if evaluates only its selected branch\n");
        terminal_write("Lists: list map filter; functions: (lambda (x) expr) or (λ ...)\n");
        terminal_write("Editor: (vi (file path)); i/a/o/O, h/j/k/l or arrows, x, u, :w/:q\n");
        terminal_write("System info: list /system/info for boot, features, memory, storage\n");
        terminal_write("Syscalls: (syscall 9) gets ticks; (syscall 0 (text \"hello\")) writes text\n");
        terminal_write("Use (syscall 3 (file /path)) to read; see /system/info/syscalls\n");
        terminal_write("Syscall arguments are typed; raw user pointers and syscall 1 are blocked.\n");
        terminal_write("(exit [status]) exits Clearsh; Pioneer restarts it.\n");
        terminal_write("File and console I/O use syscalls; math, logic, and lists are intrinsic.\n");
        terminal_write("Pioneer runs scripts from /system/config/pioneer/boot at startup.\n");
        terminal_write("(uptime) returns seconds; (shutdown) or (poweroff) powers off.\n");
        terminal_write("Files max 4096 bytes; paths max 255 chars. (install) prints guidance.\n");
        terminal_write("Disk image: run ./build.sh disk on the host; no physical install yet.\n");
        terminal_write("Input: arrows edit, Up/Down browse history; brackets/quotes auto-close.\n");
        return make_value(VALUE_NIL);
    }

    if (id == FUNCTION_LS) {
        char path[PATH_MAX];
        char entries[TEXT_MAX];
        size_t length = 0;
        if (count > 1) return make_error("ls expects zero or one path");
        if (count == 0) {
            string_copy(path, current_directory, sizeof(path));
        } else if (args[0].type == VALUE_FILE) {
            string_copy(path, args[0].data.text, sizeof(path));
        } else {
            return make_error("ls expects a file value");
        }
        if (ramfs_list(path, entries, sizeof(entries) - 1, &length)) {
            return make_error("directory could not be listed");
        }
        entries[length] = '\0';
        if (length) terminal_write(entries);
        else terminal_write("(empty)\n");
        return make_value(VALUE_NIL);
    }

    if (id == FUNCTION_CD) {
        char entries[TEXT_MAX];
        size_t length = 0;
        if (count != 1 || args[0].type != VALUE_FILE) {
            return make_error("cd expects one directory file");
        }
        if (ramfs_list(args[0].data.text, entries,
                       sizeof(entries) - 1, &length)) {
            return make_error("directory does not exist");
        }
        string_copy(current_directory, args[0].data.text,
                    sizeof(current_directory));
        value result = make_value(VALUE_TEXT);
        string_copy(result.data.text, current_directory,
                    sizeof(result.data.text));
        return result;
    }

    if (id == FUNCTION_PWD) {
        if (count != 0) return make_error("pwd takes no arguments");
        value result = make_value(VALUE_TEXT);
        string_copy(result.data.text, current_directory,
                    sizeof(result.data.text));
        return result;
    }

    if (id == FUNCTION_EDIT) {
        if (count != 1 || args[0].type != VALUE_FILE) {
            return make_error("edit expects one file value");
        }
        return edit_file(&args[0]);
    }

    if (id == FUNCTION_INSTALL) {
        if (count != 0) return make_error("install takes no arguments");
        terminal_write("Build a bootable disk image on the host with ./build.sh disk.\n");
        terminal_write("Clearsh cannot yet partition or overwrite a physical disk.\n");
        return make_value(VALUE_NIL);
    }

    if (id == FUNCTION_EXIT) {
        int32_t status = 0;
        if (count > 1 ||
            (count == 1 && !get_number(&args[0], &status))) {
            return make_error("exit expects an optional numeric status");
        }
        clearsh_syscall(SYS_EXIT, (uint32_t)status, 0, 0, 0);
        return make_error("process exit returned unexpectedly");
    }

    if (id == FUNCTION_SHUTDOWN) {
        if (count != 0) return make_error("shutdown takes no arguments");
        clearsh_syscall(SYS_SHUTDOWN, 0, 0, 0, 0);
        return make_error("shutdown request returned unexpectedly");
    }

    return make_error("unknown function");
}
