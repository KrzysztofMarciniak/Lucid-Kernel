#ifndef LUCID_IMPL_HEAP_BUMPHEAP_INTERNAL_H
#define LUCID_IMPL_HEAP_BUMPHEAP_INTERNAL_H

#include <stdint.h>

/* heap lives at a fixed virtual range, above the identity-mapped low
   4MB so it never collides with kernel image / boot structures /
   vmm's pt_pool. Grows page-by-page on demand via vmm_map. */
#define HEAP_START 0x400000   /* 4MB */
#define HEAP_MAX_SIZE (16 * 1024 * 1024) /* 16MB heap ceiling */

extern uint32_t heap_current;  /* next free byte, bump pointer */
extern uint32_t heap_mapped_end; /* highest virt addr currently mapped */

#endif
