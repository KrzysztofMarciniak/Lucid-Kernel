#ifndef LUCID_RECIPE_SYSCALL_FS_READ_H
#define LUCID_RECIPE_SYSCALL_FS_READ_H

#include "syscall_abi.h"

/* SYS_FS_READ: EBX path, ECX path length, EDX output, ESI capacity. */
#define SYS_FS_READ 3u

#endif
