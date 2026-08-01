#ifndef LUCID_RECIPE_PMM_H
#define LUCID_RECIPE_PMM_H

#include "status.h"
#include <stdint.h>

lucid_status pmm_pre(void);
lucid_status pmm_init(void);
lucid_status pmm_post(void);

/* allocate/free one physical frame. Returns 0 (physical addr 0 is
   reserved/never handed out) on allocation failure. */
uint32_t pmm_alloc_frame(void);
void pmm_free_frame(uint32_t addr);

#endif
