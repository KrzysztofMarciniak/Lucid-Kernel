#include "clearsh_internal.h"
#include "recipes/terminal.h"

typedef struct {
    char buffer[EDITOR_MAX];
    size_t length;
    size_t cursor;
    int valid;
} editor_undo;

static char editor_screen[TERMINAL_MAX_ROWS][TERMINAL_MAX_COLUMNS];
static char editor_previous_screen[TERMINAL_MAX_ROWS]
                                  [TERMINAL_MAX_COLUMNS];
static uint32_t editor_previous_columns;
static uint32_t editor_previous_rows;
static size_t editor_view_top;
static int editor_screen_valid;

static void editor_append_text(char *row,
                               size_t width,
                               size_t *column,
                               const char *text)
{
    while (*text && *column < width) {
        row[(*column)++] = *text++;
    }
}

static void editor_append_number(char *row,
                                 size_t width,
                                 size_t *column,
                                 size_t number)
{
    char digits[12];
    size_t count = 0;
    do {
        digits[count++] = (char)('0' + number % 10);
        number /= 10;
    } while (number && count < sizeof(digits));
    while (count && *column < width) {
        row[(*column)++] = digits[--count];
    }
}

static size_t editor_append_output_number(char *output, size_t number)
{
    char digits[12];
    size_t count = 0;
    do {
        digits[count++] = (char)('0' + number % 10);
        number /= 10;
    } while (number && count < sizeof(digits));
    for (size_t i = 0; i < count; i++) {
        output[i] = digits[count - i - 1];
    }
    return count;
}

static void editor_render_row(uint32_t row,
                              uint32_t columns,
                              uint32_t first,
                              uint32_t last)
{
    char output[TERMINAL_MAX_COLUMNS + 12];
    size_t used = 0;
    output[used++] = '\x1B';
    output[used++] = '[';
    used += editor_append_output_number(output + used, row + 1);
    output[used++] = ';';
    used += editor_append_output_number(output + used, first + 1);
    output[used++] = 'H';
    for (uint32_t column = first; column <= last; column++) {
        output[used++] = editor_screen[row][column];
    }
    output[used] = '\0';
    terminal_write(output);
    for (uint32_t column = 0; column < columns; column++) {
        editor_previous_screen[row][column] =
            editor_screen[row][column];
    }
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
    uint32_t dimensions = terminal_get_size();
    uint32_t columns = dimensions >> 16;
    uint32_t rows = dimensions & 0xFFFFu;
    if (columns == 0 || columns > TERMINAL_MAX_COLUMNS) {
        columns = TERMINAL_MAX_COLUMNS;
    }
    if (rows == 0 || rows > TERMINAL_MAX_ROWS) {
        rows = TERMINAL_MAX_ROWS;
    }

    size_t cursor_line = 0;
    size_t cursor_column = 0;
    for (size_t i = 0; i < cursor; i++) {
        if (buffer[i] == '\n') {
            cursor_line++;
            cursor_column = 0;
        } else {
            cursor_column++;
        }
    }

    uint32_t mode_row = rows > 2 ? rows - 3 : rows - 1;
    uint32_t status_row = rows > 1 ? rows - 2 : rows - 1;
    uint32_t content_rows = mode_row > 2 ? mode_row - 2 : 0;
    if (content_rows) {
        if (cursor_line < editor_view_top) {
            editor_view_top = cursor_line;
        } else if (cursor_line >= editor_view_top + content_rows) {
            editor_view_top = cursor_line - content_rows + 1;
        }
    } else {
        editor_view_top = cursor_line;
    }

    for (uint32_t row = 0; row < rows; row++) {
        for (uint32_t column = 0; column < columns; column++) {
            editor_screen[row][column] = ' ';
        }
    }

    size_t column = 0;
    editor_append_text(editor_screen[0], columns, &column, "Clearsh vi: ");
    editor_append_text(editor_screen[0], columns, &column, path);
    if (modified && column < columns) editor_screen[0][column] = '*';

    if (rows > 1) {
        column = 0;
        editor_append_text(editor_screen[1], columns, &column, "  ");
        editor_append_number(editor_screen[1], columns, &column,
                             cursor_line + 1);
        if (column < columns) editor_screen[1][column++] = ':';
        editor_append_number(editor_screen[1], columns, &column,
                             cursor_column + 1);
    }

    size_t line_start = 0;
    for (size_t line = 0; line < editor_view_top; line++) {
        while (line_start < length && buffer[line_start] != '\n') {
            line_start++;
        }
        if (line_start < length) line_start++;
    }
    for (uint32_t visible_line = 0;
         visible_line < content_rows && line_start <= length;
         visible_line++) {
        size_t line_end = line_start;
        while (line_end < length && buffer[line_end] != '\n') line_end++;
        size_t line_length = line_end - line_start;
        size_t line_number = editor_view_top + visible_line;
        size_t horizontal_offset = line_number == cursor_line &&
                                   cursor_column >= columns
            ? cursor_column - columns + 1
            : 0;
        uint32_t screen_row = visible_line + 2;
        for (uint32_t x = 0; x < columns; x++) {
            size_t character_column = horizontal_offset + x;
            if (line_number == cursor_line &&
                character_column == cursor_column) {
                editor_screen[screen_row][x] = '>';
            } else if (character_column < line_length) {
                char character = buffer[line_start + character_column];
                editor_screen[screen_row][x] =
                    character >= 32 ? character : ' ';
            }
        }
        if (line_end == length) break;
        line_start = line_end + 1;
    }

    column = 0;
    editor_append_text(editor_screen[mode_row], columns, &column,
        insert_mode
            ? "-- INSERT --  Esc: normal mode"
            : "-- NORMAL --  i/a: insert  :w  :q  :wq  :q!");
    if (status) {
        column = 0;
        editor_append_text(editor_screen[status_row], columns, &column,
                           status);
    }

    if (!editor_screen_valid ||
        editor_previous_columns != columns ||
        editor_previous_rows != rows) {
        for (uint32_t row = 0; row < rows; row++) {
            for (uint32_t x = 0; x < columns; x++) {
                editor_previous_screen[row][x] = ' ';
            }
        }
        editor_previous_columns = columns;
        editor_previous_rows = rows;
        editor_screen_valid = 1;
    }
    for (uint32_t row = 0; row < rows; row++) {
        uint32_t first = columns;
        uint32_t last = 0;
        for (uint32_t x = 0; x < columns; x++) {
            if (editor_screen[row][x] != editor_previous_screen[row][x]) {
                if (first == columns) first = x;
                last = x;
            }
        }
        if (first != columns) {
            editor_render_row(row, columns, first, last);
        }
    }
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
    editor_screen_valid = 0;
    editor_view_top = 0;
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
