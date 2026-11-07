#ifndef LUCID_RECIPE_SYSCALL_FS_WRITE_H
#define LUCID_RECIPE_SYSCALL_FS_WRITE_H

#include "syscall_abi.h"

/* SYS_FS_WRITE: EBX path, ECX path length, EDX input, ESI byte count. */
#define SYS_FS_WRITE 4u

#endif
