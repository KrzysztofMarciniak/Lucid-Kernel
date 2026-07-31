/* gdt.c - Global Descriptor Table :: flat implementation
 *
 * Flat memory model: every segment covers the full 4GB address space,
 * so segmentation is a no-op and real protection comes from paging.
 * Entry 5 is a TSS descriptor -- needed the moment ring 3 code takes
 * an interrupt, since the CPU loads SS0:ESP0 from it on that
 * transition. See tss.c.
 *
 * Phase split:
 *   gdt_pre  - nothing to check for a flat table, always ready.
 *   gdt_init - build the table + TSS descriptor. Does NOT touch the
 *              CPU (no lgdt, no ltr) -- table only exists in memory
 *              until every recipe's _pre has passed.
 *   gdt_post - lgdt, reload segment registers, ltr.
 */
#include "recipes/gdt.h"
#include "gdt_defs.h"
#include "internal.h"

#define GDT_ENTRIES 6

static struct gdt_entry gdt[GDT_ENTRIES] __attribute__((aligned(8)));
static struct gdt_ptr   gdtp;

void gdt_set_gate(int num, uint32_t base, uint32_t limit,
                   uint8_t access, uint8_t gran_flags) {
    gdt[num].base_low    = base & 0xFFFF;
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high   = (base >> 24) & 0xFF;
    gdt[num].limit_low   = limit & 0xFFFF;
    gdt[num].granularity = ((limit >> 16) & 0x0F) | (gran_flags & 0xF0);
    gdt[num].access      = access;
}

/* Reloading CS requires a far jump -- no C construct for that.
 * "memory" clobber: lgdt reads gdt[], compiler can't see that. */
static inline void gdt_flush(uint32_t gdt_ptr_addr) {
    __asm__ volatile (
        "lgdt (%0)\n"
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        "ljmp $0x08, $1f\n"
        "1:\n"
        :
        : "r"(gdt_ptr_addr)
        : "eax", "memory"
    );
}

void gdt_set_kernel_stack(uint32_t esp0) {
    tss_set_kernel_stack(esp0);
}
lucid_status gdt_pre(void) {
    return (lucid_status) {
        .ok = 0,
        .msg = "Global Descriptor Table validation"
    };
}

lucid_status gdt_init(void) {
    gdtp.limit = (sizeof(struct gdt_entry) * GDT_ENTRIES) - 1;
    gdtp.base  = (uint32_t)&gdt;

    gdt_set_gate(0, 0, 0, 0, 0); /* null descriptor, required */
    gdt_set_gate(1, 0, 0xFFFFFFFF,
        SEG_PRESENT | SEG_DPL0 | SEG_S_NORMAL | SEG_EXEC | SEG_RW,
        GRAN_4K_32BIT); /* kernel code: 0x08 */
    gdt_set_gate(2, 0, 0xFFFFFFFF,
        SEG_PRESENT | SEG_DPL0 | SEG_S_NORMAL | SEG_RW,
        GRAN_4K_32BIT); /* kernel data: 0x10 */
    gdt_set_gate(3, 0, 0xFFFFFFFF,
        SEG_PRESENT | SEG_DPL3 | SEG_S_NORMAL | SEG_EXEC | SEG_RW,
        GRAN_4K_32BIT); /* user code: 0x18 */
    gdt_set_gate(4, 0, 0xFFFFFFFF,
        SEG_PRESENT | SEG_DPL3 | SEG_S_NORMAL | SEG_RW,
        GRAN_4K_32BIT); /* user data: 0x20 */

    tss_install(5, SEL_KERNEL_DATA, 0);

    return (lucid_status) {
        .ok = 0,
        .msg = "GDT table built (6 entries: null, kernel code, kernel data, user code, user data, TSS)"
    };
}

lucid_status gdt_post(void) {
    gdt_flush((uint32_t)&gdtp);
    tss_load();

    return (lucid_status) {
        .ok = 0,
        .msg = "GDT loaded, segment registers reloaded, TSS installed"
    };
}

