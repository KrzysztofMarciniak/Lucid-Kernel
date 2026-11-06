#include "recipes/syscall.h"

#include <stdint.h>

static uint32_t text_length(const char *text)
{
    uint32_t length = 0;
    while (text[length]) length++;
    return length;
}

static void write_text(const char *text)
{
    lucid_user_syscall(
        SYS_WRITE,
        (uint32_t)text,
        text_length(text),
        0,
        0
    );
}

static void write_spaces(uint32_t count)
{
    char spaces[76];
    uint32_t chunk = count < sizeof(spaces) ? count : sizeof(spaces);
    for (uint32_t i = 0; i < chunk; i++) spaces[i] = ' ';
    while (count) {
        chunk = count < sizeof(spaces) ? count : sizeof(spaces);
        lucid_user_syscall(SYS_WRITE, (uint32_t)spaces, chunk, 0, 0);
        count -= chunk;
    }
}

static void write_banner_border(uint32_t width, uint32_t indent)
{
    char line[78];
    line[0] = '+';
    for (uint32_t i = 1; i < width - 1; i++) line[i] = '-';
    line[width - 1] = '+';
    line[width] = '\n';
    line[width + 1] = '\0';
    write_spaces(indent);
    write_text(line);
}

static void write_banner_line(uint32_t width,
                              uint32_t indent,
                              const char *text)
{
    char line[78];
    uint32_t content_capacity = width - 4;
    uint32_t length = text_length(text);
    if (length > content_capacity) length = content_capacity;

    uint32_t used = 0;
    line[used++] = '|';
    line[used++] = ' ';
    for (uint32_t i = 0; i < length; i++) line[used++] = text[i];
    for (uint32_t i = length; i < content_capacity; i++) {
        line[used++] = ' ';
    }
    line[used++] = ' ';
    line[used++] = '|';
    line[used++] = '\n';
    line[used] = '\0';

    write_spaces(indent);
    write_text(line);
}

static void exit_process(uint32_t status)
{
    lucid_user_syscall(SYS_EXIT, status, 0, 0, 0);
    for (;;) { }
}

static void show_boot_splash(void)
{
    uint32_t dimensions = lucid_user_syscall(
        SYS_GET_TERMINAL_SIZE, 0, 0, 0, 0
    );
    uint32_t columns = dimensions >> 16;
    if (columns < 8) columns = 80;

    uint32_t width = columns < 76 ? columns : 76;
    uint32_t indent = (columns - width) / 2;
    write_text("\n");
    write_banner_border(width, indent);
    write_banner_line(width, indent, "PIONEER");
    write_banner_line(width, indent, "");
    write_banner_line(width, indent,
                      "\"Freedom so often means that one");
    write_banner_line(width, indent, "isn't needed anywhere.\"");
    write_banner_line(width, indent,
                      "- Willa Cather, O Pioneers!");
    write_banner_line(width, indent, "");
    write_banner_line(width, indent,
                      "[ OK ]  Pioneer supervisor online");
    write_banner_line(width, indent, "[ .. ]  Starting desktop session");
    write_banner_border(width, indent);
    write_text("\n");
}

static void run_child(const char *name, const char *path)
{
    uint32_t pid = lucid_user_syscall(SYS_FORK, 0, 0, 0, 0);
    if (pid == SYS_UNKNOWN_TEST) {
        write_text("Pioneer: fork failed\n");
        exit_process(1);
    }
    if (pid == 0) {
        uint32_t length = text_length(path);
        lucid_user_syscall(SYS_EXEC, (uint32_t)path, length, 0, 0);
        write_text("Pioneer: exec failed\n");
        exit_process(127);
    }
    if (lucid_user_syscall(SYS_WAIT, pid, 0, 0, 0) == SYS_UNKNOWN_TEST) {
        write_text("Pioneer: wait failed\n");
        exit_process(1);
    }
    write_text("  [ EXIT ]  ");
    write_text(name);
    write_text(" session ended\n");
}

__attribute__((section(".entry")))
void pioneer_user_entry(void)
{
    show_boot_splash();
    for (;;) {
        static const char brain[] = "/system/bin/brain";
        static const char eye[] = "/system/bin/eye";
        write_text("  [ RUN  ]  Brain desktop manager\n");
        run_child("Brain", brain);
        write_text("  [ RUN  ]  Eye terminal session\n");
        run_child("Eye", eye);
    }
}
