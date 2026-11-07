#ifndef LUCID_RECIPE_SYSCALL_FS_STAT_H
#define LUCID_RECIPE_SYSCALL_FS_STAT_H

#include "syscall_abi.h"

/* SYS_FS_STAT: EBX path, ECX path length; returns 1 for directories, 0 files. */
#define SYS_FS_STAT 15u

#endif
