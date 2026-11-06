#include "clearsh_internal.h"

typedef struct {
    char buffer[EDITOR_MAX];
    size_t length;
    size_t cursor;
    int valid;
} editor_undo;

static size_t editor_previous_line_count;

static void editor_write_number(size_t number)
{
    char digits[12];
    size_t count = 0;
    do {
        digits[count++] = (char)('0' + number % 10);
        number /= 10;
    } while (number && count < sizeof(digits));
    while (count) terminal_putc(digits[--count]);
}

static void editor_save_undo(editor_undo *undo,
                             const char *buffer,
                             size_t length,
                             size_t cursor)
{
    for (size_t i = 0; i < length; i++) undo->buffer[i] = buffer[i];
    undo->length = length;
    undo->cursor = cursor;
    undo->valid = 1;
}

static void editor_render(const char *path,
                          const char *buffer,
                          size_t length,
                          size_t cursor,
                          int insert_mode,
                          int modified,
                          const char *status)
{
    terminal_write("\x1B[H");
    terminal_write("Clearsh vi: ");
    terminal_write(path);
    if (modified) terminal_putc('*');
    terminal_write("\x1B[K");
    size_t line = 1;
    size_t column = 1;
    for (size_t i = 0; i < cursor; i++) {
        if (buffer[i] == '\n') {
            line++;
            column = 1;
        } else {
            column++;
        }
    }
    terminal_write("  ");
    editor_write_number(line);
    terminal_putc(':');
    editor_write_number(column);
    terminal_write("\x1B[K");
    terminal_putc('\n');
    size_t line_count = 1;
    for (size_t i = 0; i <= length; i++) {
        if (i == cursor) terminal_putc('>');
        if (i < length) {
            if (buffer[i] == '\n') {
                terminal_write("\x1B[K");
                terminal_putc('\n');
                line_count++;
            } else {
                terminal_putc(buffer[i]);
            }
        }
    }
    terminal_write("\x1B[K");
    terminal_write("\n\n");
    terminal_write(insert_mode
        ? "-- INSERT --  Esc: normal mode"
        : "-- NORMAL --  i/a: insert  :w  :q  :wq  :q!");
    terminal_write("\x1B[K");
    terminal_putc('\n');
    if (status && status[0]) {
        terminal_write(status);
    }
    terminal_write("\x1B[K");
    terminal_putc('\n');
    if (line_count < editor_previous_line_count) {
        terminal_write("\x1B[J");
    }
    editor_previous_line_count = line_count;
}

static void editor_move_vertical(const char *buffer,
                                 size_t length,
                                 size_t *cursor,
                                 int down)
{
    size_t line_start = *cursor;
    while (line_start > 0 && buffer[line_start - 1] != '\n') line_start--;
    size_t column = *cursor - line_start;

    if (!down) {
        if (line_start == 0) return;
        size_t previous_end = line_start - 1;
        size_t previous_start = previous_end;
        while (previous_start > 0 &&
               buffer[previous_start - 1] != '\n') previous_start--;
        size_t previous_length = previous_end - previous_start;
        *cursor = previous_start +
            (column < previous_length ? column : previous_length);
        return;
    }

    size_t line_end = line_start;
    while (line_end < length && buffer[line_end] != '\n') line_end++;
    if (line_end == length) return;
    size_t next_start = line_end + 1;
    size_t next_end = next_start;
    while (next_end < length && buffer[next_end] != '\n') next_end++;
    size_t next_length = next_end - next_start;
    *cursor = next_start + (column < next_length ? column : next_length);
}

static void editor_insert(char *buffer,
                          size_t capacity,
                          size_t *length,
                          size_t *cursor,
                          char c)
{
    if (*length >= capacity) return;
    for (size_t i = *length; i > *cursor; i--) {
        buffer[i] = buffer[i - 1];
    }
    buffer[*cursor] = c;
    (*length)++;
    (*cursor)++;
}

static void editor_delete(char *buffer,
                          size_t *length,
                          size_t cursor,
                          int before_cursor)
{
    if (before_cursor) {
        if (cursor == 0) return;
        cursor--;
    } else if (cursor >= *length) {
        return;
    }
    for (size_t i = cursor; i + 1 < *length; i++) {
        buffer[i] = buffer[i + 1];
    }
    (*length)--;
}

static int editor_command(const value *file,
                          char *buffer,
                          size_t length,
                          size_t cursor,
                          int *insert_mode,
                          int *modified,
                          char *status)
{
    char command[8];
    size_t used = 0;
    command[0] = '\0';
    for (;;) {
        char display[16] = ":";
        string_copy(display + 1, command, sizeof(display) - 1);
        editor_render(file->data.text, buffer, length, cursor,
                      *insert_mode, *modified, display);

        uint32_t key = terminal_read_key();
        if (key == 0x1B) {
            status[0] = '\0';
            return 0;
        }
        if (key == '\n') break;
        if (key == '\b' || key == SYS_KEY_DELETE) {
            if (used) command[--used] = '\0';
            continue;
        }
        if (key >= 32 && key <= 126 && used + 1 < sizeof(command)) {
            command[used++] = (char)key;
            command[used] = '\0';
        }
    }

    int save = string_equal(command, "w") ||
               string_equal(command, "wq");
    int quit = string_equal(command, "q") ||
               string_equal(command, "q!") ||
               string_equal(command, "wq");
    if (!save && !quit) {
        string_copy(status, "Unknown command", 64);
        return 0;
    }
    if (quit && !save && *modified && !string_equal(command, "q!")) {
        string_copy(status, "Unsaved changes; use :q! to discard", 64);
        return 0;
    }
    if (save && ramfs_write(file->data.text, buffer, length)) {
        string_copy(status, "Could not write file", 64);
        return 0;
    }
    if (save) {
        *modified = 0;
        string_copy(status, "Written", 64);
    }
    if (quit) return 1;
    return 0;
}

value edit_file(const value *file)
{
    char buffer[EDITOR_MAX];
    editor_undo undo;
    undo.valid = 0;
    size_t length = 0;
    if (ramfs_read(file->data.text, buffer, sizeof(buffer), &length)) {
        length = 0;
    }

    size_t cursor = 0;
    int insert_mode = 0;
    int modified = 0;
    char status[64] = "";
    editor_previous_line_count = 0;
    terminal_clear();
    for (;;) {
        editor_render(file->data.text, buffer, length, cursor,
                      insert_mode, modified, status);
        uint32_t key = terminal_read_key();
        if (insert_mode) {
            if (key == 0x1B) {
                insert_mode = 0;
                if (cursor > 0) cursor--;
                status[0] = '\0';
            } else if (key == SYS_KEY_LEFT) {
                if (cursor > 0) cursor--;
            } else if (key == SYS_KEY_RIGHT) {
                if (cursor < length) cursor++;
            } else if (key == SYS_KEY_UP) {
                editor_move_vertical(buffer, length, &cursor, 0);
            } else if (key == SYS_KEY_DOWN) {
                editor_move_vertical(buffer, length, &cursor, 1);
            } else if (key == '\b') {
                if (cursor > 0) {
                    editor_save_undo(&undo, buffer, length, cursor);
                    editor_delete(buffer, &length, cursor, 1);
                    cursor--;
                    modified = 1;
                }
            } else if (key == SYS_KEY_DELETE) {
                if (cursor < length) {
                    editor_save_undo(&undo, buffer, length, cursor);
                    editor_delete(buffer, &length, cursor, 0);
                    modified = 1;
                }
            } else if (key == '\n' || (key >= 32 && key <= 255)) {
                if (length < sizeof(buffer)) {
                    editor_save_undo(&undo, buffer, length, cursor);
                    editor_insert(buffer, sizeof(buffer), &length, &cursor,
                                  key == '\n' ? '\n' : (char)key);
                    modified = 1;
                } else {
                    string_copy(status, "Buffer full (4096 bytes)", sizeof(status));
                }
            }
            continue;
        }

        if (key == 'i') {
            insert_mode = 1;
            status[0] = '\0';
        } else if (key == 'a') {
            if (cursor < length) cursor++;
            insert_mode = 1;
            status[0] = '\0';
        } else if (key == 'h' || key == SYS_KEY_LEFT) {
            if (cursor > 0) cursor--;
        } else if (key == 'l' || key == SYS_KEY_RIGHT) {
            if (cursor < length) cursor++;
        } else if (key == 'k' || key == SYS_KEY_UP) {
            editor_move_vertical(buffer, length, &cursor, 0);
        } else if (key == 'j' || key == SYS_KEY_DOWN) {
            editor_move_vertical(buffer, length, &cursor, 1);
        } else if (key == 'x') {
            if (cursor < length) {
                editor_save_undo(&undo, buffer, length, cursor);
                editor_delete(buffer, &length, cursor, 0);
                modified = 1;
            }
        } else if (key == '0' || key == SYS_KEY_HOME) {
            while (cursor > 0 && buffer[cursor - 1] != '\n') cursor--;
        } else if (key == '$' || key == SYS_KEY_END) {
            while (cursor < length && buffer[cursor] != '\n') cursor++;
        } else if (key == 'o' || key == 'O') {
            if (length < sizeof(buffer)) {
                editor_save_undo(&undo, buffer, length, cursor);
                if (key == 'o') {
                    while (cursor < length && buffer[cursor] != '\n') cursor++;
                    editor_insert(buffer, sizeof(buffer), &length, &cursor, '\n');
                } else {
                    while (cursor > 0 && buffer[cursor - 1] != '\n') cursor--;
                    editor_insert(buffer, sizeof(buffer), &length, &cursor, '\n');
                    cursor--;
                }
                insert_mode = 1;
                modified = 1;
            }
        } else if (key == 'u') {
            if (undo.valid) {
                for (size_t i = 0; i < undo.length; i++) {
                    buffer[i] = undo.buffer[i];
                }
                length = undo.length;
                cursor = undo.cursor;
                undo.valid = 0;
                modified = 1;
            } else {
                string_copy(status, "Nothing to undo", sizeof(status));
            }
        } else if (key == ':') {
            if (editor_command(file, buffer, length, cursor,
                               &insert_mode, &modified, status)) {
                terminal_clear();
                return make_value(VALUE_NIL);
            }
        } else {
            string_copy(status, "Normal: i/a, h/j/k/l, x, :w, :q, :wq",
                        sizeof(status));
        }
    }
}
