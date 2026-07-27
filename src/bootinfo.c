/* bootinfo.c - boot-time CPU/memory stats
 *
 * Two data sources:
 *   - CPU vendor/model: the `cpuid` instruction. No C equivalent, so
 *     tiny inline asm, same as in/out earlier.
 *   - Memory size: the multiboot info structure GRUB leaves for us,
 *     pointed to by EBX at kernel entry (boot.s passes it through as
 *     our one argument). We requested the MEMINFO flag in the
 *     multiboot header, so GRUB fills in mem_lower/mem_upper for us.
 */

#include <stdint.h>
#include <stddef.h>

extern void terminal_writestring(const char* s);

/* Only the fields we actually read; the real struct has more after
 * this, but C struct access only needs correct offsets for the
 * fields we touch. */
struct multiboot_info {
    uint32_t flags;
    uint32_t mem_lower; /* KB, if flags bit 0 is set */
    uint32_t mem_upper; /* KB, if flags bit 0 is set */
} __attribute__((packed));

static inline void cpuid(uint32_t leaf, uint32_t* eax, uint32_t* ebx,
                          uint32_t* ecx, uint32_t* edx) {
    __asm__ volatile ("cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf));
}

static void print_uint(uint32_t n) {
    char buf[11];
    int i = 10;
    buf[10] = '\0';
    if (n == 0) {
        terminal_writestring("0");
        return;
    }
    while (n > 0 && i > 0) {
        buf[--i] = '0' + (n % 10);
        n /= 10;
    }
    terminal_writestring(&buf[i]);
}

void print_boot_stats(uint32_t mb_info_addr) {
    terminal_writestring("Lucid Kernel\n");

    /* --- CPU vendor string (leaf 0) --- */
    uint32_t eax, ebx, ecx, edx;
    cpuid(0, &eax, &ebx, &ecx, &edx);

    char vendor[13];
    ((uint32_t*)vendor)[0] = ebx;
    ((uint32_t*)vendor)[1] = edx;
    ((uint32_t*)vendor)[2] = ecx;
    vendor[12] = '\0';

    terminal_writestring("CPU vendor: ");
    terminal_writestring(vendor);
    terminal_writestring("\n");

    /* --- CPU brand string (leaves 0x80000002-4), if supported --- */
    uint32_t max_ext;
    cpuid(0x80000000, &max_ext, &ebx, &ecx, &edx);

    if (max_ext >= 0x80000004) {
        uint32_t brand[13];
        cpuid(0x80000002, &brand[0], &brand[1], &brand[2], &brand[3]);
        cpuid(0x80000003, &brand[4], &brand[5], &brand[6], &brand[7]);
        cpuid(0x80000004, &brand[8], &brand[9], &brand[10], &brand[11]);
        char* brand_str = (char*)brand;
        brand_str[48] = '\0';

        terminal_writestring("CPU model:  ");
        terminal_writestring(brand_str);
        terminal_writestring("\n");
    }

    /* --- Memory, from GRUB's multiboot info --- */
    struct multiboot_info* mbi = (struct multiboot_info*)mb_info_addr;

    terminal_writestring("Memory:     ");
    if (mbi->flags & 0x1) {
        uint32_t total_kb = mbi->mem_lower + mbi->mem_upper;
        print_uint(total_kb / 1024);
        terminal_writestring(" MB (lower=");
        print_uint(mbi->mem_lower);
        terminal_writestring("KB, upper=");
        print_uint(mbi->mem_upper);
        terminal_writestring("KB)\n");
    } else {
        terminal_writestring("not reported by bootloader\n");
    }

    terminal_writestring("====================\n");
}
