#ifndef LUCID_RECIPE_SYSCALL_WRITE_H
#define LUCID_RECIPE_SYSCALL_WRITE_H

#include "syscall_abi.h"

/* SYS_WRITE: EBX points to bytes, ECX is their length; returns bytes written. */
#define SYS_WRITE 0u
#define SYS_WRITE_MAX 4096u

#ifdef LUCID_USER
static inline uint32_t lucid_write(const void *bytes, uint32_t length)
{
    return lucid_user_syscall(
        SYS_WRITE, (uint32_t)bytes, length, 0, 0
    );
}
#endif

#endif
