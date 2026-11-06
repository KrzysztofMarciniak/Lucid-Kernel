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

static void show_boot_splash(void)
{
    write_text(
        "\n"
        "  +--------------------------------------------------------+\n"
        "  |                                                        |\n"
        "  |      LUCID KERNEL     /     32-BIT X86                 |\n"
        "  |      Small kernel. Direct interfaces.                 |\n"
        "  |                                                        |\n"
        "  |   [ OK ]  Pioneer supervisor online                   |\n"
        "  |   [ .. ]  Starting desktop session                    |\n"
        "  |                                                        |\n"
        "  +--------------------------------------------------------+\n"
    );
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
