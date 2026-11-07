#ifndef LUCID_RECIPE_SYSCALL_WAIT_H
#define LUCID_RECIPE_SYSCALL_WAIT_H

#include "syscall_abi.h"

/* SYS_WAIT: EBX child PID; blocks until the child exits. */
#define SYS_WAIT 12u

#endif
