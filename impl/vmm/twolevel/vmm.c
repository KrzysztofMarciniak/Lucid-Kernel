#include "recipes/vmm.h"
#include "recipes/pmm.h"
#include "recipes/status.h"
#include "internal.h"

static uint32_t page_directory[PD_ENTRIES] __attribute__((aligned(4096)));
uint32_t *active_pd = page_directory;

static uint32_t boot_pt[PT_ENTRIES] __attribute__((aligned(4096)));

static uint32_t pt_pool[PT_POOL_COUNT][PT_ENTRIES]
    __attribute__((aligned(4096)));

static int pt_pool_used = 0;


void tlb_flush(uint32_t virt)
{
    __asm__ volatile (
        "invlpg (%0)"
        :
        : "r"(virt)
        : "memory"
    );
}


uint32_t *get_or_create_pt(uint32_t virt)
{
    uint32_t pd_idx = PD_INDEX(virt);

    if (!(active_pd[pd_idx] & PDE_PRESENT)) {

        if (pt_pool_used >= PT_POOL_COUNT) {
            return 0;
        }

        uint32_t *new_pt = pt_pool[pt_pool_used++];

        for (int i = 0; i < PT_ENTRIES; i++) {
            new_pt[i] = 0;
        }

        /*
         * Page directory entry itself must not automatically
         * become user accessible.
         *
         * It is promoted to user only when a user page exists.
         */
        active_pd[pd_idx] =
            (uint32_t)new_pt |
            PDE_PRESENT |
            PDE_WRITABLE;
    }

    return (uint32_t *)(active_pd[pd_idx] & ~0xFFF);
}


lucid_status vmm_pre(void)
{
    return LUCID_OK;
}


lucid_status vmm_init(void)
{
    for (int i = 0; i < PD_ENTRIES; i++) {
        page_directory[i] = 0;
    }

    for (int i = 0; i < PT_ENTRIES; i++) {
        boot_pt[i] = 0;
    }


    /*
     * Kernel identity map.
     *
     * Supervisor only.
     */
    for (int i = 0; i < PT_ENTRIES; i++) {
        uint32_t phys = i * PAGE_SIZE;

        boot_pt[i] =
            phys |
            PDE_PRESENT |
            PDE_WRITABLE;
    }


    page_directory[0] =
        (uint32_t)boot_pt |
        PDE_PRESENT |
        PDE_WRITABLE;


    return LUCID_OK_MSG(
        "Page directory built, low 4MB identity-mapped"
    );
}


lucid_status vmm_post(void)
{
    __asm__ volatile (
        "mov %0, %%cr3"
        :
        : "r"(active_pd)
    );


    uint32_t cr0;

    __asm__ volatile (
        "mov %%cr0, %0"
        : "=r"(cr0)
    );

    cr0 |= 0x80000000;


    __asm__ volatile (
        "mov %0, %%cr0"
        :
        : "r"(cr0)
    );


    return LUCID_OK_MSG(
        "Paging enabled (cr3 loaded, PG bit set)"
    );
}


lucid_status vmm_map_phys(uint32_t virt, uint32_t phys, uint32_t flags)
{
    virt = PAGE_ALIGN_DOWN(virt);
    phys = PAGE_ALIGN_DOWN(phys);


    if (!vmm_validate_mapping(virt, flags)) {

        return LUCID_FAIL_EX(
            1,
            "vmm_map_phys invalid mapping",
            "kernel/user address class mismatch",
            "use VMM_USER only with user virtual addresses"
        );
    }


    uint32_t *pt = get_or_create_pt(virt);

    if (!pt) {
        return LUCID_FAIL_EX(
            1,
            "vmm_map_phys failed",
            "no page table available",
            "increase PT_POOL_COUNT"
        );
    }


    uint32_t pt_idx = PT_INDEX(virt);


    if (pt[pt_idx] & PDE_PRESENT) {

        return LUCID_FAIL_EX(
            1,
            "vmm_map_phys overwrite",
            "virtual page already mapped",
            "use vmm_unmap before remapping"
        );
    }


    uint32_t entry_flags = PDE_PRESENT;


    if (flags & VMM_WRITABLE)
        entry_flags |= PDE_WRITABLE;


    if (flags & VMM_USER) {

        entry_flags |= PDE_USER;


        /*
         * User pages require the page directory entry
         * to also have the U/S bit set.
         */
        uint32_t pd_idx = PD_INDEX(virt);

        active_pd[pd_idx] |= PDE_USER;
    }


    pt[pt_idx] = phys | entry_flags;


    tlb_flush(virt);


    return LUCID_OK;
}


lucid_status vmm_map(uint32_t virt, uint32_t flags)
{
    uint32_t phys = pmm_alloc_frame();

    if (phys == 0) {

        return LUCID_FAIL_EX(
            1,
            "vmm_map failed",
            "PMM out of frames",
            "free memory or reduce allocation"
        );
    }


    return vmm_map_phys(
        virt,
        phys,
        flags
    );
}


lucid_status vmm_map_range(
    uint32_t virt,
    uint32_t phys,
    uint32_t length,
    uint32_t flags
)
{
    uint32_t start = PAGE_ALIGN_DOWN(virt);
    uint32_t end   = PAGE_ALIGN_UP(virt + length);
    uint32_t p     = PAGE_ALIGN_DOWN(phys);


    for (uint32_t v = start; v < end; v += PAGE_SIZE, p += PAGE_SIZE) {

        lucid_status st =
            vmm_map_phys(v, p, flags);

        if (st.ok != 0) {
            return st;
        }
    }


    return LUCID_OK;
}


lucid_status vmm_unmap(uint32_t virt)
{
    virt = PAGE_ALIGN_DOWN(virt);

    uint32_t pd_idx = PD_INDEX(virt);


    if (!(active_pd[pd_idx] & PDE_PRESENT)) {
        return LUCID_OK;
    }


    uint32_t *pt =
        (uint32_t *)(active_pd[pd_idx] & ~0xFFF);


    pt[PT_INDEX(virt)] = 0;


    tlb_flush(virt);


    return LUCID_OK;
}


int vmm_check(uint32_t virt, uint32_t *out_phys)
{
    virt = PAGE_ALIGN_DOWN(virt);

    uint32_t pd_idx = PD_INDEX(virt);

    if (!(active_pd[pd_idx] & PDE_PRESENT))
        return 0;

    if (!(active_pd[pd_idx] & PDE_USER))
        return 0;

    uint32_t *pt =
        (uint32_t *)(active_pd[pd_idx] & ~0xFFF);

    uint32_t entry = pt[PT_INDEX(virt)];

    if (!(entry & PDE_PRESENT))
        return 0;

    if (!(entry & PDE_USER))
        return 0;

    if (out_phys)
        *out_phys = entry & ~0xFFF;

    return 1;
}


int vmm_make_user(uint32_t virt)
{
    virt = PAGE_ALIGN_DOWN(virt);

    uint32_t pd_idx = PD_INDEX(virt);


    if (!(active_pd[pd_idx] & PDE_PRESENT)) {
        return 0;
    }


    uint32_t *pt =
        (uint32_t *)(active_pd[pd_idx] & ~0xFFF);


    uint32_t entry =
        pt[PT_INDEX(virt)];


    if (!(entry & PDE_PRESENT)) {
        return 0;
    }


    entry |= PDE_USER;

    pt[PT_INDEX(virt)] = entry;


    active_pd[pd_idx] |= PDE_USER;


    tlb_flush(virt);


    return 1;
}
