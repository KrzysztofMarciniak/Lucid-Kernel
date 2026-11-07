#ifndef LUCID_RECIPE_SYSCALL_ABI_H
#define LUCID_RECIPE_SYSCALL_ABI_H

#include <stdint.h>

#define SYS_UNKNOWN_TEST 0xFFFFFFFFu

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

#endif
