#ifndef LUCID_RECIPE_SYSCALL_H
#define LUCID_RECIPE_SYSCALL_H

/* REQUIRES: idt, terminal, usermode, vmm, ramfs, timer */

#include "status.h"
#include <stdint.h>

#include "syscall_abi.h"
#include "syscall_fs_read.h"
#include "syscall_fs_write.h"

#define SYS_WM_OPEN 0x300u
#define SYS_WM_EXIT 0x301u
#define SYS_WM_REAP 0x302u
#define SYS_KEY_UP 0x11u
#define SYS_KEY_DOWN 0x12u
#define SYS_KEY_LEFT 0x13u
#define SYS_KEY_RIGHT 0x14u
#define SYS_KEY_DELETE 0x15u
#define SYS_KEY_HOME 0x16u
#define SYS_KEY_END 0x17u
#define SYS_KEY_CTRL_BASE 0x100u
#define SYS_KEY_CTRL_LEFT 0x120u
#define SYS_KEY_CTRL_RIGHT 0x121u
#define SYS_KEY_CTRL_A (SYS_KEY_CTRL_BASE + 1u)
#define SYS_KEY_CTRL_E (SYS_KEY_CTRL_BASE + 5u)
#define SYS_KEY_CTRL_K (SYS_KEY_CTRL_BASE + 11u)
#define SYS_KEY_CTRL_L (SYS_KEY_CTRL_BASE + 12u)
#define SYS_KEY_CTRL_U (SYS_KEY_CTRL_BASE + 21u)
#define SYS_KEY_CTRL_W (SYS_KEY_CTRL_BASE + 23u)
#define SYS_FILE_MAX 4096u
#define SYS_LINE_MAX 2048u
#define SYS_COMPLETION_NAMES_MAX 1024u
#define SYS_PROMPT_MAX 264u

#include "syscall_files.h"

#ifdef LUCID_USER
static inline int lucid_read_config_path(const char *config_path,
                                         char path[SYS_PATH_MAX],
                                         uint32_t *path_length)
{
    char config[SYS_PATH_MAX + 2];
    uint32_t length = lucid_fs_read(config_path, config, sizeof(config));
    if (length == SYS_UNKNOWN_TEST || length >= sizeof(config)) return 0;

    while (length &&
           (config[length - 1] == '\n' || config[length - 1] == '\r')) {
        length--;
    }
    if (!length || length >= SYS_PATH_MAX || config[0] != '/') return 0;
    for (uint32_t i = 0; i < length; i++) {
        if (config[i] <= ' ' || config[i] == 0x7F) return 0;
        path[i] = config[i];
    }
    path[length] = '\0';
    *path_length = length;
    return 1;
}
#endif

/* Data written to /system/terminals/readline_config before reading readline. */
typedef struct {
    uint32_t function_names_length;
    char current_directory[SYS_PATH_MAX];
    char prompt[SYS_PROMPT_MAX];
    char function_names[SYS_COMPLETION_NAMES_MAX];
} sys_readline_completion;

lucid_status syscall_pre(void);
lucid_status syscall_init(void);
lucid_status syscall_post(void);

#endif
