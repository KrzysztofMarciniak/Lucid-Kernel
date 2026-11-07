#ifndef LUCID_RECIPE_SYSCALL_EXEC_H
#define LUCID_RECIPE_SYSCALL_EXEC_H

#include "syscall_abi.h"

/* SYS_EXEC: EBX path, ECX path length; replaces the calling process. */
#define SYS_EXEC 11u

#endif
