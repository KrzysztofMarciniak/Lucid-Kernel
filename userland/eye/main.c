#include "recipes/syscall.h"

#include <stdint.h>

static void write_text(const char *text)
{
    uint32_t length = 0;
    while (text[length]) length++;
    lucid_user_syscall(SYS_WRITE, (uint32_t)text, length, 0, 0);
}

__attribute__((section(".entry")))
void eye_user_entry(void)
{
    static const char shell[] = "/system/bin/clearsh";
    lucid_user_syscall(SYS_EXEC, (uint32_t)shell, sizeof(shell) - 1, 0, 0);
    write_text("Eye: could not start Clearsh\n");
    lucid_user_syscall(SYS_EXIT, 127, 0, 0, 0);
    for (;;) { }
}
