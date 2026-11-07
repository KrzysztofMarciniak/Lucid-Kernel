#ifndef LUCID_RECIPE_SYSCALL_EXIT_H
#define LUCID_RECIPE_SYSCALL_EXIT_H

#include "syscall_abi.h"

/* SYS_EXIT: EBX is the process exit status; does not return to user mode. */
#define SYS_EXIT 1u

#endif
