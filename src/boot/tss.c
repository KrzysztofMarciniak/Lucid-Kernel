#include "tss.h"
#include "gdt.h"
#include "gdt_defs.h"

struct tss_entry {
    uint32_t prev_tss;
    uint32_t esp0;
    uint32_t ss0;
    uint32_t esp1, ss1, esp2, ss2;
    uint32_t cr3, eip, eflags;
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base;
} __attribute__((packed));

static struct tss_entry tss;

void tss_install(int gdt_index, uint16_t kernel_ss, uint32_t kernel_esp0)
{
    uint32_t base  = (uint32_t)&tss;
    uint32_t limit = sizeof(tss) - 1;

    /* 'tss' is static, so it is already zero-initialized. */

    tss.ss0 = kernel_ss;
    tss.esp0 = kernel_esp0;
    tss.iomap_base = sizeof(tss);

    gdt_set_gate(
        gdt_index,
        base,
        limit,
        SEG_PRESENT | SEG_DPL0 | SEG_TSS_TYPE,
        0
    );

    __asm__ volatile ("ltr %0" : : "r"((uint16_t)SEL_TSS) : "memory");
}

void tss_set_kernel_stack(uint32_t esp0)
{
    tss.esp0 = esp0;
}

