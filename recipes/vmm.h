#ifndef LUCID_RECIPE_VMM_H
#define LUCID_RECIPE_VMM_H

#include "status.h"
#include <stdint.h>

#define VMM_PRESENT   0x1
#define VMM_WRITABLE  0x2
#define VMM_USER      0x4

lucid_status vmm_pre(void);
lucid_status vmm_init(void);
lucid_status vmm_post(void);

/* REQUIRES: pmm */

/* map virt -> a PMM-allocated frame, with given flags */
lucid_status vmm_map(uint32_t virt, uint32_t flags);

/* map virt -> a specific physical address (MMIO, DMA, etc) */
lucid_status vmm_map_phys(uint32_t virt, uint32_t phys, uint32_t flags);

/* map a run of `length` bytes starting at virt -> phys, single flags
   applied to every page. length is rounded up to a page boundary.
   convenience wrapper -- loops over vmm_map_phys internally. */
lucid_status vmm_map_range(uint32_t virt, uint32_t phys, uint32_t length, uint32_t flags);

lucid_status vmm_unmap(uint32_t virt);

/* is virt currently mapped? if out_phys is non-NULL, fill it in */
int vmm_check(uint32_t virt, uint32_t *out_phys);

#endif
