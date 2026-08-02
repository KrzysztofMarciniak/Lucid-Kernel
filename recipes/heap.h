#ifndef LUCID_RECIPE_HEAP_H
#define LUCID_RECIPE_HEAP_H

#include "status.h"
#include <stddef.h>

/* REQUIRES: pmm, vmm */

lucid_status heap_pre(void);
lucid_status heap_init(void);
lucid_status heap_post(void);

void *kmalloc(size_t size);
/* no kfree yet -- bump allocator only. Freeing needs a real
   free-list/block-header scheme, added later once something actually
   needs to reclaim memory (not needed for one-shot kernel structures
   like process/file tables). */

#endif
