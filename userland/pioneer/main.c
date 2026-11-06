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

static void exit_process(uint32_t status)
{
    lucid_user_syscall(SYS_EXIT, status, 0, 0, 0);
    for (;;) { }
}

__attribute__((section(".entry")))
void pioneer_user_entry(void)
{
    write_text("Pioneer: launching /system/bin/clearsh\n");

    for (;;) {
        uint32_t pid = lucid_user_syscall(SYS_FORK, 0, 0, 0, 0);
        if (pid == SYS_UNKNOWN_TEST) {
            write_text("Pioneer: fork failed\n");
            exit_process(1);
        }

        if (pid == 0) {
            static const char path[] = "/system/bin/clearsh";
            lucid_user_syscall(
                SYS_EXEC,
                (uint32_t)path,
                sizeof(path) - 1,
                0,
                0
            );
            write_text("Pioneer: exec failed\n");
            exit_process(127);
        }

        if (lucid_user_syscall(SYS_WAIT, pid, 0, 0, 0) ==
            SYS_UNKNOWN_TEST) {
            write_text("Pioneer: wait failed\n");
            exit_process(1);
        }
    }
}
