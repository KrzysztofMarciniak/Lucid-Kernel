#ifndef LUCID_RECIPE_KERNEL_PANIC_H
#define LUCID_RECIPE_KERNEL_PANIC_H

#include "recipes/status.h"
#include "stdint.h"

lucid_status kernel_panic_pre(void);
lucid_status kernel_panic_init(void);
lucid_status kernel_panic_post(void);

#endif
