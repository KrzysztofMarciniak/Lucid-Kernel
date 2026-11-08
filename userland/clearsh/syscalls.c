#include "clearsh_internal.h"

static const char terminal_screen_path[] = "/system/terminals/current";
static const char empty_text[] = "";
static char terminal_screen[SYS_FILE_MAX];

static void terminal_append(const char *text, size_t length)
{
    while (length) {
        size_t chunk_length = length > SYS_FILE_MAX
            ? SYS_FILE_MAX
            : length;
        uint32_t screen_length = lucid_fs_read(
            terminal_screen_path, terminal_screen, sizeof(terminal_screen)
        );
        if (screen_length == SYS_UNKNOWN_TEST ||
            screen_length > sizeof(terminal_screen)) {
            return;
        }

        if (screen_length + chunk_length > sizeof(terminal_screen)) {
            size_t drop = screen_length + chunk_length -
                          sizeof(terminal_screen);
            while (drop < screen_length &&
                   terminal_screen[drop] != '\n') {
                drop++;
            }
            if (drop < screen_length) drop++;
            if (drop > screen_length) drop = screen_length;
            screen_length -= (uint32_t)drop;
            for (size_t i = 0; i < screen_length; i++) {
                terminal_screen[i] = terminal_screen[i + drop];
            }
        }

        for (size_t i = 0; i < chunk_length; i++) {
            terminal_screen[screen_length + i] = text[i];
        }
        screen_length += (uint32_t)chunk_length;
        lucid_fs_write(terminal_screen_path, terminal_screen, screen_length);
        text += chunk_length;
        length -= chunk_length;
    }
}

void terminal_write(const char *text)
{
    if (clearsh_quiet_output) return;
    size_t length = 0;
    while (text[length]) length++;
    terminal_append(text, length);
}

void terminal_putc(char c)
{
    if (clearsh_quiet_output) return;
    terminal_append(&c, 1);
}

uint32_t terminal_read_key(void)
{
    return lucid_read_key();
}

uint32_t terminal_get_size(void)
{
    return lucid_terminal_size();
}

int terminal_file_clear(void)
{
    if (clearsh_quiet_output) return 1;
    return lucid_fs_write(terminal_screen_path, empty_text, 0) !=
        SYS_UNKNOWN_TEST;
}

int ramfs_read(const char *path,
               char *buffer,
               size_t capacity,
               size_t *length)
{
    uint32_t result = lucid_fs_read(path, buffer, (uint32_t)capacity);
    if (result == SYS_UNKNOWN_TEST || result > capacity) return 1;
    *length = result;
    return 0;
}

int ramfs_write(const char *path, const char *data, size_t length)
{
    return lucid_fs_write(path, data, (uint32_t)length) == SYS_UNKNOWN_TEST;
}

int ramfs_list(const char *path,
               char *buffer,
               size_t capacity,
               size_t *length)
{
    uint32_t result = lucid_fs_read(path, buffer, (uint32_t)capacity);
    if (result == SYS_UNKNOWN_TEST || result > capacity) return 1;
    *length = result;
    return 0;
}

int ramfs_is_directory(const char *path)
{
    return lucid_fs_is_directory(path);
}
