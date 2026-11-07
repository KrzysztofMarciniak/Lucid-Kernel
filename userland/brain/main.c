#include "recipes/syscall.h"

#include <stdint.h>

static void write_text(const char *text)
{
    uint32_t length = 0;
    while (text[length]) length++;
    lucid_write(text, length);
}

static uint32_t launch_terminal(void)
{
    static const char config_path[] = "/system/config/defaults/terminal";
    char terminal[SYS_PATH_MAX];
    uint32_t terminal_length;
    if (!lucid_read_config_path(
            config_path, terminal, &terminal_length)) {
        return SYS_UNKNOWN_TEST;
    }

    uint32_t pid = lucid_user_syscall(SYS_FORK, 0, 0, 0, 0);
    if (pid != 0 && pid != SYS_UNKNOWN_TEST) {
        return pid;
    }
    if (pid == SYS_UNKNOWN_TEST) return pid;

    lucid_user_syscall(
        SYS_EXEC, (uint32_t)terminal, terminal_length, 0, 0
    );
    write_text("Brain: could not start the configured terminal\n");
    lucid_user_syscall(SYS_EXIT, 127, 0, 0, 0);
    for (;;) { }
}

static void launch_statusbar_worker(void)
{
    static const char statusbar[] = "/system/bin/statusbar";
    uint32_t pid = lucid_user_syscall(SYS_FORK, 0, 0, 0, 0);
    if (pid == SYS_UNKNOWN_TEST) {
        write_text("Brain: no process slot for the status-bar worker\n");
        return;
    }
    if (pid != 0) return;

    lucid_user_syscall(
        SYS_EXEC, (uint32_t)statusbar, sizeof(statusbar) - 1, 0, 0
    );
    write_text("Brain: could not start the status-bar worker\n");
    lucid_user_syscall(SYS_EXIT, 127, 0, 0, 0);
    for (;;) { }
}

__attribute__((section(".entry")))
void brain_user_entry(void)
{
    launch_statusbar_worker();
    if (launch_terminal() == SYS_UNKNOWN_TEST) {
        write_text(
            "Brain: could not launch terminal; check "
            "/system/config/defaults/terminal\n"
        );
    }

    for (;;) {
        uint32_t event = lucid_user_syscall(SYS_READKEY, 0, 0, 0, 0);
        if (event == SYS_WM_OPEN) {
            if (launch_terminal() == SYS_UNKNOWN_TEST) {
                write_text(
                    "Brain: could not launch terminal; check "
                    "/system/config/defaults/terminal\n"
                );
            }
        } else if (event == SYS_WM_REAP) {
            write_text("Brain: reaping terminal\n");
            lucid_user_syscall(SYS_REAP, 0, 0, 0, 0);
        } else if (event == SYS_WM_EXIT) {
            write_text("Brain: exiting\n");
            lucid_user_syscall(SYS_EXIT, 0, 0, 0, 0);
            for (;;) { }
        }
    }
}
