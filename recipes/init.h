#ifndef LUCID_RECIPE_INIT_H
#define LUCID_RECIPE_INIT_H

/* REQUIRES: terminal, ramfs, syscall, usermode, vmm */

#include "status.h"

lucid_status init_pre(void);
lucid_status init_init(void);
lucid_status init_post(void);
void init_run(void) __attribute__((noreturn));

#endif
