#ifndef LUCID_IMPL_VMM_TWOLEVEL_INTERNAL_H
#define LUCID_IMPL_VMM_TWOLEVEL_INTERNAL_H

#include <stdint.h>
#include "recipes/vmm.h"

#define PAGE_SIZE VMM_PAGE_SIZE

#define PT_ENTRIES      1024
#define PD_ENTRIES      1024


/*
 * x86 page table flags.
 */
#define PDE_PRESENT     0x1
#define PDE_WRITABLE    0x2
#define PDE_USER        0x4


/*
 * Virtual address layout.
 *
 * Kernel:
 *   0x00100000 - 0x003FFFFF
 *   supervisor only
 *
 * User:
 *   0x00400000+
 *   requires VMM_USER
 */
#ifndef LOW_RESERVED_MB
#define LOW_RESERVED_MB 4
#endif

#define USER_BASE (LOW_RESERVED_MB * 1024 * 1024)
#define KERNEL_BASE 0x00100000

#define PD_INDEX(v)  (((v) >> 22) & 0x3FF)
#define PT_INDEX(v)  (((v) >> 12) & 0x3FF)

#define PAGE_ALIGN_DOWN(a) ((a) & ~(PAGE_SIZE - 1))
#define PAGE_ALIGN_UP(a)   (((a) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))


/*
 * Page tables themselves must always live inside the permanently
 * identity-mapped low 4MB.
 *
 * They are allocated from a static pool instead of PMM because
 * vmm writes directly through the virtual address and requires the
 * physical frame to already be identity mapped.
 */
#define PT_POOL_COUNT 64


/*
 * Address classification helpers.
 */
static inline int vmm_is_user_address(uint32_t virt)
{
    return virt >= USER_BASE;
}


static inline int vmm_is_kernel_address(uint32_t virt)
{
    return virt < USER_BASE;
}


/*
 * Validate mapping intent.
 *
 * Rules:
 *
 * 1. User pages must be placed in user virtual range.
 *
 * 2. Kernel pages must never become user accessible.
 *
 * This catches mistakes like mapping kernel .text as ring3 code.
 */
static inline int vmm_validate_mapping(uint32_t virt, uint32_t flags)
{
    if (flags & VMM_KERNEL) {
        return !(flags & VMM_USER);
    }

    if (flags & VMM_USER) {
        if (!vmm_is_user_address(virt)) {
            return 0;
        }
    } else {
        if (!vmm_is_kernel_address(virt)) {
            return 0;
        }
    }

    return 1;
}


extern uint32_t *active_pd;


void tlb_flush(uint32_t virt);

uint32_t *get_or_create_pt(uint32_t virt);

void vmm_debug_dump(uint32_t virt);

void vmm_debug_format_page(
    char *buf,
    uint32_t size,
    uint32_t virt
);


#endif
