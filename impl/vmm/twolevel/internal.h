#ifndef LUCID_IMPL_VMM_TWOLEVEL_INTERNAL_H
#define LUCID_IMPL_VMM_TWOLEVEL_INTERNAL_H

#include <stdint.h>
#include "recipes/vmm.h"

#define PAGE_SIZE VMM_PAGE_SIZE
#define PT_ENTRIES      1024
#define PD_ENTRIES      1024

#define PDE_PRESENT     0x1
#define PDE_WRITABLE    0x2
#define PDE_USER        0x4

#define PD_INDEX(v)  (((v) >> 22) & 0x3FF)
#define PT_INDEX(v)  (((v) >> 12) & 0x3FF)
#define PAGE_ALIGN_DOWN(a) ((a) & ~(PAGE_SIZE - 1))
#define PAGE_ALIGN_UP(a)   (((a) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))

/* page tables themselves must always live inside the permanently
   identity-mapped low 4MB, since vmm writes to them via a raw
   physical-address cast (only safe while identity-mapped). A static
   pool avoids ever handing pmm_alloc_frame() the job of picking a
   page table's storage, since pmm doesn't know about this constraint. */
#define PT_POOL_COUNT 64  /* max page tables ever creatable -- 64 covers 256MB of mapped range */

extern uint32_t *active_pd;

void tlb_flush(uint32_t virt);
uint32_t *get_or_create_pt(uint32_t virt);

#endif
