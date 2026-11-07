#ifndef LUCID_RECIPE_SYSCALL_H
#define LUCID_RECIPE_SYSCALL_H

/* REQUIRES: idt, terminal, usermode, vmm, ramfs, timer */

#include "status.h"
#include <stdint.h>

#include "syscall_abi.h"
#include "syscall_exit.h"
#include "syscall_readline.h"
#include "syscall_fs_read.h"
#include "syscall_fs_write.h"
#include "syscall_fs_list.h"
#include "syscall_readkey.h"
#include "syscall_clear.h"
#include "syscall_shutdown.h"
#include "syscall_getticks.h"
#include "syscall_fork.h"
#include "syscall_exec.h"
#include "syscall_wait.h"
#include "syscall_reap.h"
#include "syscall_terminal_size.h"
#include "syscall_fs_stat.h"
#include "syscall_write.h"

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
#define SYS_PATH_MAX 256u
#define SYS_FILE_MAX 4096u
#define SYS_LINE_MAX 2048u
#define SYS_COMPLETION_NAMES_MAX 1024u
#define SYS_PROMPT_MAX 264u

/* Optional SYS_READLINE metadata. Pass its address in EDX and its size in
 * ESI; pass zero in both registers to retain plain line editing. */
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
