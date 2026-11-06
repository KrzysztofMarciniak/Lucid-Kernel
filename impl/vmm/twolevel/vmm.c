#include "recipes/vmm.h"
#include "recipes/pmm.h"
#include "recipes/terminal.h"
#include "recipes/status.h"
#include "internal.h"

#define VMM_MAX_SPACES 8
#define VMM_COPY_SOURCE 0xFFC00000u
#define VMM_COPY_DESTINATION 0xFFC01000u

struct vmm_space {
    uint32_t *directory;
    int in_use;
    int kernel;
};

static uint32_t page_directory[PD_ENTRIES] __attribute__((aligned(4096)));
uint32_t *active_pd = page_directory;

static uint32_t boot_pt[PT_ENTRIES] __attribute__((aligned(4096)));

static uint32_t pt_pool[PT_POOL_COUNT][PT_ENTRIES]
    __attribute__((aligned(4096)));

static uint8_t pt_pool_used[PT_POOL_COUNT];
static uint32_t space_directories[VMM_MAX_SPACES][PD_ENTRIES]
    __attribute__((aligned(4096)));
static vmm_space kernel_space;
static vmm_space space_pool[VMM_MAX_SPACES];
static vmm_space *active_space;

static uint32_t *allocate_page_table(void)
{
    for (uint32_t i = 0; i < PT_POOL_COUNT; i++) {
        if (pt_pool_used[i]) continue;
        pt_pool_used[i] = 1;
        for (uint32_t j = 0; j < PT_ENTRIES; j++) {
            pt_pool[i][j] = 0;
        }
        return pt_pool[i];
    }
    return 0;
}

static void release_page_table(uint32_t *table)
{
    for (uint32_t i = 0; i < PT_POOL_COUNT; i++) {
        if (table != pt_pool[i]) continue;
        pt_pool_used[i] = 0;
        return;
    }
}


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
        uint32_t *new_pt = allocate_page_table();
        if (!new_pt) return 0;

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
    for (uint32_t i = 0; i < PT_POOL_COUNT; i++) {
        pt_pool_used[i] = 0;
    }
    for (int i = 0; i < PD_ENTRIES; i++) {
        page_directory[i] = 0;
    }

    kernel_space.directory = page_directory;
    kernel_space.in_use = 1;
    kernel_space.kernel = 1;
    active_space = &kernel_space;
    active_pd = page_directory;
    for (uint32_t i = 0; i < VMM_MAX_SPACES; i++) {
        space_pool[i].directory = space_directories[i];
        space_pool[i].in_use = 0;
        space_pool[i].kernel = 0;
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

    if (!get_or_create_pt(VMM_COPY_SOURCE) ||
        !get_or_create_pt(VMM_COPY_DESTINATION)) {
        return LUCID_FAIL_EX(
            1,
            "VMM scratch mapping unavailable",
            "page-table pool exhausted",
            "increase PT_POOL_COUNT"
        );
    }

    uint32_t framebuffer_base;
    uint32_t framebuffer_length;
    if (terminal_framebuffer_mapping(&framebuffer_base, &framebuffer_length)) {
        uint32_t map_start = PAGE_ALIGN_DOWN(framebuffer_base);
        uint32_t map_end = PAGE_ALIGN_UP(framebuffer_base + framebuffer_length);
        uint32_t low_identity_end = PT_ENTRIES * PAGE_SIZE;

        if (map_start < low_identity_end) {
            map_start = low_identity_end;
        }
        if (map_start < map_end) {
            lucid_status st = vmm_map_range(
                map_start,
                map_start,
                map_end - map_start,
                VMM_KERNEL | VMM_WRITABLE
            );
            if (st.ok != 0) {
                return st;
            }
        }

        return LUCID_OK_MSG("Page directory built; framebuffer mapped");
    }

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


    lucid_status st = vmm_map_phys(
        virt,
        phys,
        flags
    );
    if (st.ok != 0) pmm_free_frame(phys);
    return st;
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

int vmm_check_user_writable(uint32_t virt)
{
    virt = PAGE_ALIGN_DOWN(virt);
    uint32_t pd_idx = PD_INDEX(virt);

    if (!(active_pd[pd_idx] & PDE_PRESENT) ||
        !(active_pd[pd_idx] & PDE_USER) ||
        !(active_pd[pd_idx] & PDE_WRITABLE)) {
        return 0;
    }

    uint32_t *pt = (uint32_t *)(active_pd[pd_idx] & ~0xFFF);
    uint32_t entry = pt[PT_INDEX(virt)];
    return (entry & (PDE_PRESENT | PDE_USER | PDE_WRITABLE)) ==
        (PDE_PRESENT | PDE_USER | PDE_WRITABLE);
}

vmm_space *vmm_space_current(void)
{
    return active_space;
}

static lucid_status copy_frame(uint32_t source, uint32_t destination)
{
    vmm_unmap(VMM_COPY_SOURCE);
    vmm_unmap(VMM_COPY_DESTINATION);

    lucid_status st = vmm_map_phys(
        VMM_COPY_SOURCE,
        source,
        VMM_KERNEL | VMM_WRITABLE
    );
    if (st.ok != 0) return st;

    st = vmm_map_phys(
        VMM_COPY_DESTINATION,
        destination,
        VMM_KERNEL | VMM_WRITABLE
    );
    if (st.ok != 0) {
        vmm_unmap(VMM_COPY_SOURCE);
        return st;
    }

    uint8_t *source_view = (uint8_t *)VMM_COPY_SOURCE;
    uint8_t *destination_view = (uint8_t *)VMM_COPY_DESTINATION;
    for (uint32_t i = 0; i < VMM_PAGE_SIZE; i++) {
        destination_view[i] = source_view[i];
    }

    vmm_unmap(VMM_COPY_SOURCE);
    vmm_unmap(VMM_COPY_DESTINATION);
    return LUCID_OK;
}

lucid_status vmm_space_clone_current(vmm_space **out)
{
    if (!out || !active_space || !active_space->in_use) {
        return LUCID_FAIL_EX(
            1,
            "Address-space clone failed",
            "invalid output or active address space",
            "provide a valid address-space output pointer"
        );
    }
    *out = 0;

    vmm_space *copy = 0;
    for (uint32_t i = 0; i < VMM_MAX_SPACES; i++) {
        if (!space_pool[i].in_use) {
            copy = &space_pool[i];
            break;
        }
    }
    if (!copy) {
        return LUCID_FAIL_EX(
            1,
            "Address-space clone failed",
            "address-space pool exhausted",
            "increase VMM_MAX_SPACES"
        );
    }

    copy->in_use = 1;
    for (uint32_t i = 0; i < PD_ENTRIES; i++) {
        uint32_t entry = active_pd[i];
        copy->directory[i] =
            (entry & PDE_USER) ? 0 : entry;
    }

    for (uint32_t pd = 0; pd < PD_ENTRIES; pd++) {
        uint32_t source_entry = active_pd[pd];
        if (!(source_entry & PDE_PRESENT) || !(source_entry & PDE_USER)) {
            continue;
        }

        copy->directory[pd] = 0;
        uint32_t *source_table =
            (uint32_t *)(source_entry & ~0xFFFu);
        uint32_t *copy_table = allocate_page_table();
        if (!copy_table) {
            vmm_space_destroy(copy);
            return LUCID_FAIL_EX(
                1,
                "Address-space clone failed",
                "page-table pool exhausted",
                "increase PT_POOL_COUNT"
            );
        }
        copy->directory[pd] =
            (uint32_t)copy_table | (source_entry & 0xFFFu);

        for (uint32_t pt = 0; pt < PT_ENTRIES; pt++) {
            uint32_t source_page = source_table[pt];
            if (!(source_page & PDE_PRESENT) ||
                !(source_page & PDE_USER)) {
                copy_table[pt] = source_page;
                continue;
            }

            uint32_t frame = pmm_alloc_frame();
            if (frame == 0) {
                vmm_space_destroy(copy);
                return LUCID_FAIL_EX(
                    1,
                    "Address-space clone failed",
                    "physical frame pool exhausted",
                    "free memory or reduce process memory"
                );
            }
            lucid_status st = copy_frame(
                source_page & ~0xFFFu,
                frame
            );
            if (st.ok != 0) {
                pmm_free_frame(frame);
                vmm_space_destroy(copy);
                return st;
            }
            copy_table[pt] = frame | (source_page & 0xFFFu);
        }
    }

    *out = copy;
    return LUCID_OK_MSG("Address space cloned");
}

lucid_status vmm_space_activate(vmm_space *space)
{
    if (!space || !space->in_use) {
        return LUCID_FAIL_EX(
            1,
            "Address-space switch failed",
            "address space is not allocated",
            "activate an initialized address space"
        );
    }

    active_space = space;
    active_pd = space->directory;

    uint32_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    if (cr0 & 0x80000000u) {
        __asm__ volatile (
            "mov %0, %%cr3"
            :
            : "r"(active_pd)
            : "memory"
        );
    }
    return LUCID_OK;
}

static void clear_user_pages(vmm_space *space)
{
    for (uint32_t pd = 0; pd < PD_ENTRIES; pd++) {
        uint32_t entry = space->directory[pd];
        if (!(entry & PDE_PRESENT) || !(entry & PDE_USER)) continue;

        uint32_t *table = (uint32_t *)(entry & ~0xFFFu);
        for (uint32_t pt = 0; pt < PT_ENTRIES; pt++) {
            uint32_t page = table[pt];
            if ((page & (PDE_PRESENT | PDE_USER)) ==
                (PDE_PRESENT | PDE_USER)) {
                uint32_t address = (pd << 22) | (pt << 12);
                if (space == active_space) tlb_flush(address);
                pmm_free_frame(page & ~0xFFFu);
            }
        }
        release_page_table(table);
        space->directory[pd] = 0;
    }
}

lucid_status vmm_space_clear_user(vmm_space *space)
{
    if (!space || !space->in_use || space->kernel ||
        space != active_space) {
        return LUCID_FAIL_EX(
            1,
            "Address-space clear failed",
            "only the active user address space can be cleared",
            "activate a process address space before replacing its image"
        );
    }
    clear_user_pages(space);
    return LUCID_OK;
}

lucid_status vmm_space_destroy(vmm_space *space)
{
    if (!space || !space->in_use || space->kernel ||
        space == active_space) {
        return LUCID_FAIL_EX(
            1,
            "Address-space destroy failed",
            "address space is invalid, active, or kernel-owned",
            "switch to another address space before destroying it"
        );
    }

    clear_user_pages(space);
    for (uint32_t i = 0; i < PD_ENTRIES; i++) {
        space->directory[i] = 0;
    }
    space->in_use = 0;
    return LUCID_OK;
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
