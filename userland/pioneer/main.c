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
    lucid_write(text, text_length(text));
}

static void exit_process(uint32_t status)
{
    lucid_user_syscall(SYS_EXIT, status, 0, 0, 0);
    for (;;) { }
}

static void run_child(const char *path)
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
}

static int run_default_window_manager(void)
{
    static const char config_path[] =
        "/system/config/defaults/window_manager";
    char manager[SYS_PATH_MAX];
    uint32_t manager_length;
    if (!lucid_read_config_path(config_path, manager, &manager_length)) {
        write_text(
            "Pioneer: invalid /system/config/defaults/window_manager\n"
        );
        return 0;
    }
    run_child(manager);
    return 1;
}

#ifdef LUCID_TEST_MODE
static void test_syscall_modules(void)
{
    static const char zoom_path[] = "/system/config/clearsh/zoom";
    static const char defaults_dir[] = "/system/config/defaults";
    static const char *default_paths[] = {
        "/system/config/defaults/shell",
        "/system/config/defaults/window_manager",
        "/system/config/defaults/terminal"
    };
    static const char *default_programs[] = {
        "/system/bin/clearsh\n",
        "/system/bin/brain\n",
        "/system/bin/eye\n"
    };
    static const char *policy_paths[] = {
        "/system/config/clearsh/prompt",
        "/system/config/brain/layout",
        "/system/config/brain/keys",
        "/system/config/brain/theme"
    };
    static const char *policy_defaults[] = {
        "\xCE\xBB %p> \n",
        "desktops=10\n"
        "startup_desktop=2\n"
        "log_desktop=1\n"
        "master_ratio=60\n"
        "master_min_ratio=30\n"
        "master_max_ratio=80\n"
        "master_step=5\n",
        "key_open=alt+enter\n"
        "key_close=alt+q\n"
        "key_focus_next=alt+j\n"
        "key_focus_previous=alt+k\n"
        "key_exit=alt+shift+q\n"
        "key_master_shrink=alt+shift+h\n"
        "key_master_grow=alt+shift+l\n"
        "key_switch_desktop=alt+digit\n"
        "key_move_desktop=alt+shift+digit\n",
        "background=000000\n"
        "foreground=FFFFFF\n"
        "accent=00FF00\n"
    };
    static const char system_path[] = "/system";
    static const char syscall_dir[] = "/system/syscalls";
    static const char process_dir[] = "/system/processes";
    static const char process_list[] = "/system/processes/list";
    static const char process_kill[] = "/system/processes/kill";
    static const char self_image[] = "/system/bin/pioneer";
    char buffer[256];
    char configured_path[SYS_PATH_MAX];
    uint32_t result = lucid_user_syscall(
        SYS_FS_READ,
        (uint32_t)zoom_path,
        sizeof(zoom_path) - 1,
        (uint32_t)buffer,
        sizeof(buffer)
    );
    if (result != 2 || buffer[0] != '1' || buffer[1] != '\n') {
        for (;;) { }
    }

    result = lucid_user_syscall(
        SYS_FS_STAT,
        (uint32_t)defaults_dir,
        sizeof(defaults_dir) - 1,
        0,
        0
    );
    if (result != 1) {
        for (;;) { }
    }
    for (uint32_t i = 0;
         i < sizeof(default_paths) / sizeof(default_paths[0]); i++) {
        uint32_t configured_length;
        if (!lucid_read_config_path(
                default_paths[i], configured_path, &configured_length)) {
            for (;;) { }
        }
        uint32_t expected_length = text_length(default_programs[i]) - 1;
        if (configured_length != expected_length) {
            for (;;) { }
        }
        for (uint32_t j = 0; j < expected_length; j++) {
            if (configured_path[j] != default_programs[i][j]) {
                for (;;) { }
            }
        }
    }
    for (uint32_t i = 0;
         i < sizeof(policy_paths) / sizeof(policy_paths[0]); i++) {
        uint32_t path_length = text_length(policy_paths[i]);
        uint32_t expected_length = text_length(policy_defaults[i]);
        result = lucid_user_syscall(
            SYS_FS_READ,
            (uint32_t)policy_paths[i],
            path_length,
            (uint32_t)buffer,
            sizeof(buffer)
        );
        if (result != expected_length) {
            for (;;) { }
        }
        for (uint32_t j = 0; j < expected_length; j++) {
            if (buffer[j] != policy_defaults[i][j]) {
                for (;;) { }
            }
        }
    }

    result = lucid_user_syscall(
        SYS_FS_READ,
        (uint32_t)zoom_path,
        sizeof(zoom_path) - 1,
        0xFFFFFFFFu,
        1
    );
    if (result != SYS_UNKNOWN_TEST) {
        for (;;) { }
    }

    result = lucid_user_syscall(
        SYS_FS_LIST,
        (uint32_t)syscall_dir,
        sizeof(syscall_dir) - 1,
        (uint32_t)buffer,
        sizeof(buffer)
    );
    if (result == 0 || result == SYS_UNKNOWN_TEST) {
        for (;;) { }
    }

    result = lucid_user_syscall(
        SYS_FS_LIST,
        (uint32_t)process_dir,
        sizeof(process_dir) - 1,
        (uint32_t)buffer,
        sizeof(buffer)
    );
    if (result != 10 ||
        buffer[0] != 'k' || buffer[1] != 'i' ||
        buffer[5] != 'l' || buffer[6] != 'i') {
        for (;;) { }
    }

    result = lucid_user_syscall(
        SYS_FS_READ,
        (uint32_t)process_list,
        sizeof(process_list) - 1,
        (uint32_t)buffer,
        sizeof(buffer)
    );
    if (result == 0 || result == SYS_UNKNOWN_TEST ||
        buffer[0] != 'P' || buffer[1] != 'I' || buffer[2] != 'D') {
        for (;;) { }
    }

    result = lucid_user_syscall(
        SYS_FS_READ,
        (uint32_t)process_kill,
        sizeof(process_kill) - 1,
        (uint32_t)buffer,
        sizeof(buffer)
    );
    if (result != SYS_UNKNOWN_TEST) {
        for (;;) { }
    }

    result = lucid_user_syscall(
        SYS_FS_STAT,
        (uint32_t)process_dir,
        sizeof(process_dir) - 1,
        0,
        0
    );
    if (result != 1) {
        for (;;) { }
    }

    result = lucid_user_syscall(
        SYS_FS_WRITE,
        (uint32_t)process_kill,
        sizeof(process_kill) - 1,
        (uint32_t)self_image,
        sizeof(self_image) - 1
    );
    if (result != SYS_UNKNOWN_TEST) {
        for (;;) { }
    }

    uint32_t child_pid = lucid_user_syscall(SYS_FORK, 0, 0, 0, 0);
    if (child_pid == 0) {
        for (;;) {
            __asm__ volatile ("pause");
        }
    }
    if (child_pid == SYS_UNKNOWN_TEST) {
        for (;;) { }
    }
    result = lucid_user_syscall(
        SYS_FS_WRITE,
        (uint32_t)process_kill,
        sizeof(process_kill) - 1,
        (uint32_t)self_image,
        sizeof(self_image) - 1
    );
    if (result != sizeof(self_image) - 1) {
        for (;;) { }
    }
    result = lucid_user_syscall(SYS_WAIT, child_pid, 0, 0, 0);
    if (result != child_pid) {
        for (;;) { }
    }

    result = lucid_user_syscall(
        SYS_FS_STAT,
        (uint32_t)system_path,
        sizeof(system_path) - 1,
        0,
        0
    );
    if (result != 1) {
        for (;;) { }
    }
}
#endif

__attribute__((section(".entry")))
void pioneer_user_entry(void)
{
#ifdef LUCID_TEST_MODE
    if (lucid_write((const char *)0xFFFFFFFFu, 1) != SYS_UNKNOWN_TEST) {
        for (;;) { }
    }
    test_syscall_modules();
#endif
    for (;;) {
        static const char eye[] = "/system/bin/eye";
        if (!run_default_window_manager()) {
            exit_process(1);
        }
        run_child(eye);
    }
}
