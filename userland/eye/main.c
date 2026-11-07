#include "recipes/syscall.h"

#include <stdint.h>

static void write_text(const char *text)
{
    uint32_t length = 0;
    while (text[length]) length++;
    lucid_write(text, length);
}

__attribute__((section(".entry")))
void eye_user_entry(void)
{
    static const char config_path[] = "/system/config/defaults/shell";
    char shell[SYS_PATH_MAX];
    uint32_t shell_length;
    if (!lucid_read_config_path(config_path, shell, &shell_length)) {
        write_text("Eye: invalid /system/config/defaults/shell\n");
        lucid_user_syscall(SYS_EXIT, 127, 0, 0, 0);
        for (;;) { }
    }

    lucid_user_syscall(
        SYS_EXEC, (uint32_t)shell, shell_length, 0, 0
    );
    write_text("Eye: could not start the configured shell\n");
    lucid_user_syscall(SYS_EXIT, 127, 0, 0, 0);
    for (;;) { }
}
