#ifndef LUCID_RECIPE_SYSCALL_TERMINAL_SIZE_H
#define LUCID_RECIPE_SYSCALL_TERMINAL_SIZE_H

#include "syscall_abi.h"

/* SYS_GET_TERMINAL_SIZE: no arguments; returns columns:rows packed in EAX. */
#define SYS_GET_TERMINAL_SIZE 14u

#endif
