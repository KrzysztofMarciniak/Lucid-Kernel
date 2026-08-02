#ifndef LUCID_RECIPE_USERMODE_H
#define LUCID_RECIPE_USERMODE_H

/* REQUIRES: gdt, idt, vmm, heap */

#include "status.h"

lucid_status usermode_pre(void);
lucid_status usermode_init(void);
lucid_status usermode_post(void);

/* drops from ring0 to ring3, jumping to entry with a fresh user
   stack. Never returns to the caller directly -- control comes back
   via a syscall/interrupt trapping into ring0 (e.g. int 0x80). */
void usermode_enter(void (*entry)(void));

#endif
