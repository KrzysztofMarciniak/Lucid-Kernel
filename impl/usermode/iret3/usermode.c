#include "panic.h"
#include "recipes/usermode.h"
#include "recipes/vmm.h"
#include "recipes/gdt.h"
#include "recipes/status.h"
#include "internal.h"

static uint8_t kernel_stack[KERNEL_STACK_SIZE] __attribute__((aligned(16)));
static uint32_t kernel_stack_top;

static lucid_status map_user_stack(int clear)
{
    for (uint32_t addr = USER_STACK_TOP - USER_STACK_SIZE;
         addr < USER_STACK_TOP;
         addr += VMM_PAGE_SIZE) {
        lucid_status st = vmm_map(
            addr,
            VMM_PRESENT | VMM_WRITABLE | VMM_USER
        );
        if (st.ok != 0) return st;

        if (clear) {
            uint8_t *page = (uint8_t *)addr;
            for (uint32_t i = 0; i < VMM_PAGE_SIZE; i++) page[i] = 0;
        }
    }
    return LUCID_OK;
}

lucid_status usermode_pre(void) {
    return LUCID_OK;
}

lucid_status usermode_init(void) {
    lucid_status st = map_user_stack(0);
    if (st.ok != 0) return st;

    kernel_stack_top = (uint32_t)kernel_stack + KERNEL_STACK_SIZE;
    gdt_set_kernel_stack(kernel_stack_top);

    return LUCID_OK_MSG(
        "User stack mapped, kernel stack (esp0) set for ring3 transitions"
    );
}

lucid_status usermode_post(void) {
    uint8_t *stack = (uint8_t *)(USER_STACK_TOP - USER_STACK_SIZE);
    for (uint32_t i = 0; i < USER_STACK_SIZE; i++) stack[i] = 0;
    return LUCID_OK;
}

uint32_t usermode_kernel_stack_top(void)
{
    return kernel_stack_top;
}

void usermode_set_kernel_stack_top(uint32_t top)
{
    kernel_stack_top = top;
    gdt_set_kernel_stack(top);
}

lucid_status usermode_reset_stack(void)
{
    return map_user_stack(1);
}

lucid_status usermode_load_image(uint32_t base,
                                 const uint8_t *image,
                                 uint32_t image_size,
                                 uint32_t memory_size)
{
    uint32_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    if (!(cr0 & 0x80000000u)) {
        return LUCID_FAIL_EX(
            1,
            "User image loaded before paging",
            "image pages require active virtual memory",
            "load user images after vmm_post"
        );
    }

    if (!image || image_size == 0 || memory_size < image_size ||
        (base & (VMM_PAGE_SIZE - 1)) != 0 ||
        memory_size > 0xFFFFFFFFu - (VMM_PAGE_SIZE - 1)) {
        return LUCID_FAIL_EX(
            1,
            "User image invalid",
            "image, size, or load address is invalid",
            "provide a nonempty page-aligned image region"
        );
    }

    uint32_t mapped_size =
        (memory_size + VMM_PAGE_SIZE - 1) & ~(VMM_PAGE_SIZE - 1);
    if (base > 0xFFFFFFFFu - mapped_size) {
        return LUCID_FAIL_EX(
            1,
            "User image address overflow",
            "image region wraps the address space",
            "choose a lower user load address"
        );
    }
    uint32_t end = base + mapped_size;
    uint32_t stack_bottom = USER_STACK_TOP - USER_STACK_SIZE;
    if (base < USER_STACK_TOP && end > stack_bottom) {
        return LUCID_FAIL_EX(
            1,
            "User image overlaps stack",
            "image and user stack share virtual pages",
            "choose a separate user load address"
        );
    }

    for (uint32_t offset = 0; offset < mapped_size;
         offset += VMM_PAGE_SIZE) {
        uint32_t address = base + offset;
        lucid_status st = vmm_map(
            address,
            VMM_PRESENT | VMM_WRITABLE | VMM_USER
        );
        if (st.ok != 0) return st;

        if (!vmm_check_user_writable(address)) {
            return LUCID_FAIL_EX(
                1,
                "User image mapping failed",
                "mapped page is not user-writable",
                "check the user virtual memory range"
            );
        }

        uint8_t *destination = (uint8_t *)address;
        for (uint32_t i = 0; i < VMM_PAGE_SIZE; i++) {
            uint32_t image_offset = offset + i;
            destination[i] = image_offset < image_size
                ? image[image_offset]
                : 0;
        }
    }

    return LUCID_OK_MSG("User image loaded");
}

void usermode_enter(void (*entry)(void))
{
    uint32_t addr = (uint32_t)entry;
    if (!vmm_check(addr, 0)) {
        panic_status(
            LUCID_FAIL_EX(
                1,
                "usermode enter failed",
                "entry address is not mapped",
                "check user code page"
            )
        );
    }

    usermode_frame frame = { 0 };
    frame.gs = USER_DATA_SELECTOR;
    frame.fs = USER_DATA_SELECTOR;
    frame.es = USER_DATA_SELECTOR;
    frame.ds = USER_DATA_SELECTOR;
    frame.eip = addr;
    frame.cs = USER_CODE_SELECTOR;
    frame.eflags = 0x202;
    frame.user_esp = USER_STACK_TOP;
    frame.user_ss = USER_DATA_SELECTOR;
    usermode_resume_frame(&frame);
}
