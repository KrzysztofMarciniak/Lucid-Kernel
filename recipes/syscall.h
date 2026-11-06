#ifndef LUCID_RECIPE_SYSCALL_H
#define LUCID_RECIPE_SYSCALL_H

/* REQUIRES: idt, terminal, usermode, vmm, ramfs, timer */

#include "status.h"
#include <stdint.h>

#define SYS_WRITE 0u
#define SYS_EXIT 1u
#define SYS_READLINE 2u
#define SYS_FS_READ 3u
#define SYS_FS_WRITE 4u
#define SYS_FS_LIST 5u
#define SYS_READKEY 6u
#define SYS_CLEAR 7u
#define SYS_SHUTDOWN 8u
#define SYS_GETTICKS 9u
#define SYS_FORK 10u
#define SYS_EXEC 11u
#define SYS_WAIT 12u
#define SYS_REAP 13u
#define SYS_GET_TERMINAL_SIZE 14u
#define SYS_FS_STAT 15u
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
#define SYS_UNKNOWN_TEST 0xFFFFFFFFu
#define SYS_PATH_MAX 256u
#define SYS_FILE_MAX 4096u
#define SYS_LINE_MAX 2048u
#define SYS_WRITE_MAX 4096u
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

#ifdef LUCID_USER
static inline uint32_t lucid_user_syscall(uint32_t number,
                                           uint32_t arg1,
                                           uint32_t arg2,
                                           uint32_t arg3,
                                           uint32_t arg4)
{
    uint32_t result;
    __asm__ volatile (
        "int $0x80"
        : "=a"(result)
        : "0"(number), "b"(arg1), "c"(arg2), "d"(arg3), "S"(arg4)
        : "memory"
    );
    return result;
}
#endif

lucid_status syscall_pre(void);
lucid_status syscall_init(void);
lucid_status syscall_post(void);

#endif
