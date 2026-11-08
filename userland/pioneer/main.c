#include "recipes/syscall.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>

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
    lucid_process_exit(status);
    for (;;) { }
}

static void run_child(const char *path)
{
    uint32_t pid = lucid_process_fork();
    if (pid == SYS_UNKNOWN_TEST) {
        write_text("Pioneer: fork failed\n");
        exit_process(1);
    }
    if (pid == 0) {
        uint32_t length = text_length(path);
        lucid_process_exec(path, length);
        write_text("Pioneer: exec failed\n");
        exit_process(127);
    }
    if (lucid_process_wait(pid) == SYS_UNKNOWN_TEST) {
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
static void test_posix_file_io(void)
{
    static const char path[] = "/users/root/.lucid-posix-test";
    static const char contents[] = "Lucid POSIX file I/O\n";
    static const char suffix[] = "tail";
    char buffer[sizeof(contents) + sizeof(suffix) - 2];
    char cwd[2];

    if (getcwd(cwd, sizeof(cwd)) != cwd ||
        cwd[0] != '/' || cwd[1] != '\0') {
        for (;;) { }
    }

    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (fd < 0 ||
        write(fd, contents, sizeof(contents) - 1) !=
            (ssize_t)(sizeof(contents) - 1) ||
        lseek(fd, 0, SEEK_SET) != 0) {
        for (;;) { }
    }

    uint32_t child_pid = lucid_process_fork();
    if (child_pid == 0) {
        char first;
        if (read(fd, &first, 1) != 1 || first != contents[0]) {
            for (;;) { }
        }
        exit_process(0);
    }
    if (child_pid == SYS_UNKNOWN_TEST ||
        lucid_process_wait(child_pid) != child_pid ||
        read(fd, buffer, 1) != 1 || buffer[0] != contents[1] ||
        lseek(fd, 0, SEEK_SET) != 0 ||
        read(fd, buffer, sizeof(contents) - 1) !=
            (ssize_t)(sizeof(contents) - 1)) {
        for (;;) { }
    }
    for (uint32_t i = 0; i < sizeof(contents) - 1; i++) {
        if (buffer[i] != contents[i]) {
            for (;;) { }
        }
    }
    if (close(fd) != 0) {
        for (;;) { }
    }
    if (close(fd) != -1 || errno != EBADF) {
        for (;;) { }
    }
    if (close(-1) != -1 || errno != EBADF) {
        for (;;) { }
    }

    if (open(path, O_WRONLY | O_CREAT | O_EXCL, 0600) != -1 ||
        errno != EEXIST) {
        for (;;) { }
    }

    fd = open(path, O_WRONLY | O_APPEND);
    if (fd < 0 ||
        write(fd, suffix, sizeof(suffix) - 1) !=
            (ssize_t)(sizeof(suffix) - 1) ||
        close(fd) != 0) {
        for (;;) { }
    }

    fd = open(path, O_RDONLY);
    if (fd < 0 ||
        write(fd, contents, 1) != -1 ||
        errno != EBADF ||
        read(fd, buffer, sizeof(buffer)) !=
            (ssize_t)(sizeof(contents) + sizeof(suffix) - 2) ||
        close(fd) != 0 ||
        unlink(path) != 0 ||
        access(path, F_OK) != -1 ||
        errno != ENOENT) {
        for (;;) { }
    }
    for (uint32_t i = 0; i < sizeof(contents) - 1; i++) {
        if (buffer[i] != contents[i]) {
            for (;;) { }
        }
    }
    for (uint32_t i = 0; i < sizeof(suffix) - 1; i++) {
        if (buffer[sizeof(contents) - 1 + i] != suffix[i]) {
            for (;;) { }
        }
    }
    if (access("users/root/welcome", F_OK) != 0) {
        for (;;) { }
    }
}

static void test_file_watch(void)
{
    static const char path[] = "/users/root/.lucid-watch-test";
    static const char contents[] = "watch notification\n";
    static const char missing_path[] = "/users/root/.lucid-watch-missing";

    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (fd < 0 ||
        lucid_fs_write(
            "/system/files/watch",
            (const void *)(uintptr_t)0xFFFFFFFFu,
            sizeof(path) - 1
        ) != SYS_UNKNOWN_TEST ||
        lucid_fs_watch(missing_path) != SYS_UNKNOWN_TEST) {
        for (;;) { }
    }

    uint32_t child_pid = lucid_process_fork();
    if (child_pid == 0) {
        lucid_sleep(2);
        exit_process(
            write(fd, contents, sizeof(contents) - 1) ==
                    (ssize_t)(sizeof(contents) - 1)
                ? 0
                : 1
        );
    }
    if (child_pid == SYS_UNKNOWN_TEST ||
        lucid_fs_watch(path) != 1 ||
        lucid_process_wait(child_pid) != child_pid) {
        for (;;) { }
    }

    child_pid = lucid_process_fork();
    if (child_pid == 0) {
        lucid_sleep(2);
        exit_process(unlink(path) == 0 ? 0 : 1);
    }
    if (child_pid == SYS_UNKNOWN_TEST ||
        lucid_fs_watch(path) != 1 ||
        lucid_process_wait(child_pid) != child_pid ||
        close(fd) != 0 ||
        access(path, F_OK) != -1 ||
        errno != ENOENT) {
        for (;;) { }
    }
}

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
    static const char terminal_dir[] = "/system/terminals";
    static const char terminal_path[] = "/system/terminals/current";
    static const char terminal_message[] = "terminal-file replacement";
    static const char empty_text[] = "";
    static const char process_dir[] = "/system/processes";
    static const char process_dir_listing[] = "current\nkill\nlist\n";
    static const char process_list[] = "/system/processes/list";
    static const char process_kill[] = "/system/processes/kill";
    static const char self_image[] = "/system/bin/pioneer";
    static const char terminal_dir_listing[] =
        "current\nreadline\nreadline_config\n";
    char buffer[256];
    char terminal_contents[SYS_FILE_MAX];
    char configured_path[SYS_PATH_MAX];
    uint32_t result = lucid_fs_read(zoom_path, buffer, sizeof(buffer));
    if (result != 2 || buffer[0] != '1' || buffer[1] != '\n') {
        for (;;) { }
    }

    result = (uint32_t)lucid_fs_is_directory(defaults_dir);
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
        uint32_t expected_length = text_length(policy_defaults[i]);
        result = lucid_fs_read(policy_paths[i], buffer, sizeof(buffer));
        if (result != expected_length) {
            for (;;) { }
        }
        for (uint32_t j = 0; j < expected_length; j++) {
            if (buffer[j] != policy_defaults[i][j]) {
                for (;;) { }
            }
        }
    }

    result = lucid_fs_read(
        zoom_path, (void *)(uintptr_t)0xFFFFFFFFu, 1
    );
    if (result != SYS_UNKNOWN_TEST) {
        for (;;) { }
    }

    result = lucid_fs_read(syscall_dir, buffer, sizeof(buffer));
    if (result == 0 || result == SYS_UNKNOWN_TEST) {
        for (;;) { }
    }

    result = lucid_fs_read(terminal_dir, buffer, sizeof(buffer));
    if (result != sizeof(terminal_dir_listing) - 1) {
        for (;;) { }
    }
    for (uint32_t i = 0; i < result; i++) {
        if (buffer[i] != terminal_dir_listing[i]) {
            for (;;) { }
        }
    }

    result = lucid_fs_write(
        terminal_path, terminal_message, sizeof(terminal_message) - 1
    );
    if (result != sizeof(terminal_message) - 1) {
        for (;;) { }
    }

    result = lucid_fs_read(
        terminal_path, terminal_contents, sizeof(terminal_contents)
    );
    if (result != sizeof(terminal_message) - 1) {
        for (;;) { }
    }
    for (uint32_t i = 0; i < result; i++) {
        if (terminal_contents[i] != terminal_message[i]) {
            for (;;) { }
        }
    }

    result = lucid_fs_write(terminal_path, empty_text, 0);
    if (result != 0) {
        for (;;) { }
    }
    result = lucid_fs_read(
        terminal_path, terminal_contents, sizeof(terminal_contents)
    );
    if (result != 0) {
        for (;;) { }
    }

    result = lucid_fs_read(process_dir, buffer, sizeof(buffer));
    if (result != sizeof(process_dir_listing) - 1) {
        for (;;) { }
    }
    for (uint32_t i = 0; i < result; i++) {
        if (buffer[i] != process_dir_listing[i]) {
            for (;;) { }
        }
    }

    result = lucid_fs_read(process_list, buffer, sizeof(buffer));
    if (result == 0 || result == SYS_UNKNOWN_TEST ||
        buffer[0] != 'P' || buffer[1] != 'I' || buffer[2] != 'D') {
        for (;;) { }
    }

    result = lucid_fs_read(process_kill, buffer, sizeof(buffer));
    if (result != SYS_UNKNOWN_TEST) {
        for (;;) { }
    }

    result = (uint32_t)lucid_fs_is_directory(process_dir);
    if (result != 1) {
        for (;;) { }
    }

    result = lucid_fs_write(process_kill, self_image, sizeof(self_image) - 1);
    if (result != SYS_UNKNOWN_TEST) {
        for (;;) { }
    }

    uint32_t child_pid = lucid_process_fork();
    if (child_pid == 0) {
        for (;;) {
            __asm__ volatile ("pause");
        }
    }
    if (child_pid == SYS_UNKNOWN_TEST) {
        for (;;) { }
    }
    result = lucid_fs_write(process_kill, self_image, sizeof(self_image) - 1);
    if (result != sizeof(self_image) - 1) {
        for (;;) { }
    }
    result = lucid_process_wait(child_pid);
    if (result != child_pid) {
        for (;;) { }
    }

    result = (uint32_t)lucid_fs_is_directory(system_path);
    if (result != 1) {
        for (;;) { }
    }
    test_posix_file_io();
    test_file_watch();

    uint32_t start = lucid_get_ticks();
    uint32_t current = lucid_wait_ticks(2);
    if ((uint32_t)(current - start) < 2) {
        for (;;) { }
    }
}
#endif

__attribute__((section(".entry")))
void pioneer_user_entry(void)
{
#ifdef LUCID_TEST_MODE
    if (lucid_fs_write("/tmp/invalid-pointer",
                       (const char *)0xFFFFFFFFu, 1) != SYS_UNKNOWN_TEST) {
        for (;;) { }
    }
    test_syscall_modules();
    static const char test_success[] =
        "POSIX file descriptor tests passed\n";
    lucid_write(test_success, sizeof(test_success) - 1);
    lucid_process_exit(0);
    for (;;) { }
#endif
    for (;;) {
        static const char eye[] = "/system/bin/eye";
        if (!run_default_window_manager()) {
            exit_process(1);
        }
        run_child(eye);
    }
}
