#include "internal.h"

static void append_char(char *buf,
                        uint32_t size,
                        uint32_t *pos,
                        char c)
{
    if (*pos < size - 1) {
        buf[*pos] = c;
        (*pos)++;
    }
}
void vmm_debug_dump(uint32_t virt)
{
    uint32_t page = PAGE_ALIGN_DOWN(virt);

    uint32_t pd_idx = PD_INDEX(page);
    uint32_t pt_idx = PT_INDEX(page);

    uint32_t pde = active_pd[pd_idx];
    uint32_t pte = 0;

    if (pde & PDE_PRESENT)
    {
        uint32_t *pt = (uint32_t *)(pde & ~0xFFF);
        pte = pt[pt_idx];
    }

    panic_status(
        LUCID_FAIL_EX(
            1,
            "VMM DEBUG",
            "dump reached",
            "inspect pde/pte"
        )
    );
}
static void append_string(char *buf,
                          uint32_t size,
                          uint32_t *pos,
                          const char *s)
{
    while (*s) {
        append_char(buf, size, pos, *s++);
    }
}

static void append_hex(char *buf,
                       uint32_t size,
                       uint32_t *pos,
                       uint32_t value)
{
    static const char hex[] = "0123456789ABCDEF";

    append_string(buf, size, pos, "0x");

    for (int i = 7; i >= 0; i--) {
        append_char(
            buf,
            size,
            pos,
            hex[(value >> (i * 4)) & 0xF]
        );
    }
}

static void append_u32(char *buf,
                       uint32_t size,
                       uint32_t *pos,
                       uint32_t value)
{
    char tmp[10];
    uint32_t n = 0;

    if (value == 0) {
        append_char(buf, size, pos, '0');
        return;
    }

    while (value) {
        tmp[n++] = '0' + (value % 10);
        value /= 10;
    }

    while (n) {
        append_char(buf, size, pos, tmp[--n]);
    }
}

void vmm_debug_format_page(char *buf,
                           uint32_t size,
                           uint32_t virt)
{
    uint32_t pos = 0;

    uint32_t pd_idx = PD_INDEX(virt);
    uint32_t pt_idx = PT_INDEX(virt);

    uint32_t pde = active_pd[pd_idx];
    uint32_t pte = 0;

    if (pde & PDE_PRESENT) {
        uint32_t *pt = (uint32_t *)(pde & ~0xFFF);
        pte = pt[pt_idx];
    }

    append_string(buf, size, &pos, "virt=");
    append_hex(buf, size, &pos, virt);

    append_string(buf, size, &pos, " pd=");
    append_u32(buf, size, &pos, pd_idx);

    append_string(buf, size, &pos, " pt=");
    append_u32(buf, size, &pos, pt_idx);

    append_string(buf, size, &pos, " PDE=");
    append_hex(buf, size, &pos, pde);

    append_string(buf, size, &pos, " PTE=");
    append_hex(buf, size, &pos, pte);

    append_string(buf, size, &pos, " PDE_U=");
    append_u32(buf, size, &pos, (pde & PDE_USER) ? 1 : 0);

    append_string(buf, size, &pos, " PTE_U=");
    append_u32(buf, size, &pos, (pte & PDE_USER) ? 1 : 0);

    buf[pos] = 0;
}
