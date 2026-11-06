#ifndef LUCID_RECIPE_TIMER_H
#define LUCID_RECIPE_TIMER_H

#include "status.h"
#include <stdint.h>

/* REQUIRES: idt, pic */

lucid_status timer_pre(void);
lucid_status timer_init(void);
lucid_status timer_post(void);
uint32_t timer_get_ticks(void);

#endif
