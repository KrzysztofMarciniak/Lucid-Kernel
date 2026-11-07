#ifndef LUCID_RECIPE_SYSCALL_FORK_H
#define LUCID_RECIPE_SYSCALL_FORK_H

#include "syscall_abi.h"

/* SYS_FORK: no arguments; parent gets child PID, child resumes with zero. */
#define SYS_FORK 10u

#endif
