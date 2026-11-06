#include "recipes/syscall.h"

#include <stdint.h>

static void write_text(const char *text)
{
    uint32_t length = 0;
    while (text[length]) length++;
    lucid_user_syscall(SYS_WRITE, (uint32_t)text, length, 0, 0);
}

static uint32_t launch_eye(void)
{
    uint32_t pid = lucid_user_syscall(SYS_FORK, 0, 0, 0, 0);
    if (pid != 0 && pid != SYS_UNKNOWN_TEST) {
        write_text("Brain: Eye started\n");
        return pid;
    }
    if (pid == SYS_UNKNOWN_TEST) return pid;

    static const char path[] = "/system/bin/eye";
    lucid_user_syscall(SYS_EXEC, (uint32_t)path, sizeof(path) - 1, 0, 0);
    write_text("Brain: could not start Eye\n");
    lucid_user_syscall(SYS_EXIT, 127, 0, 0, 0);
    for (;;) { }
}

__attribute__((section(".entry")))
void brain_user_entry(void)
{
    write_text("Brain: starting Eye\n");
    if (launch_eye() == SYS_UNKNOWN_TEST) {
        write_text("Brain: could not create Eye process\n");
    }

    for (;;) {
        uint32_t event = lucid_user_syscall(SYS_READKEY, 0, 0, 0, 0);
        if (event == SYS_WM_OPEN) {
            if (launch_eye() == SYS_UNKNOWN_TEST) {
                write_text("Brain: could not create Eye process\n");
            }
        } else if (event == SYS_WM_REAP) {
            write_text("Brain: reaping Eye\n");
            lucid_user_syscall(SYS_REAP, 0, 0, 0, 0);
        } else if (event == SYS_WM_EXIT) {
            write_text("Brain: exiting\n");
            lucid_user_syscall(SYS_EXIT, 0, 0, 0, 0);
            for (;;) { }
        }
    }
}
