#include "panic.h"
#include "recipes/usermode.h"
#include "recipes/vmm.h"
#include "recipes/gdt.h"
#include "recipes/status.h"
#include "internal.h"

static uint8_t kernel_stack[KERNEL_STACK_SIZE] __attribute__((aligned(16)));

lucid_status usermode_pre(void) {
    return LUCID_OK;
}

lucid_status usermode_init(void) {
    for (uint32_t addr = USER_STACK_TOP - USER_STACK_SIZE;
         addr < USER_STACK_TOP;
         addr += VMM_PAGE_SIZE) {

        lucid_status st = vmm_map(
            addr,
            VMM_PRESENT | VMM_WRITABLE | VMM_USER
        );

        if (st.ok != 0) {
            return st;
        }
    }

    uint32_t kstack_top = (uint32_t)kernel_stack + KERNEL_STACK_SIZE;
    gdt_set_kernel_stack(kstack_top);

    return LUCID_OK_MSG(
        "User stack mapped, kernel stack (esp0) set for ring3 transitions"
    );
}

lucid_status usermode_post(void) {
    return LUCID_OK;
}


void usermode_enter(void (*entry)(void))
{
    uint32_t addr = (uint32_t)entry;
    uint32_t phys = 0;

    if (!vmm_check(addr, &phys))
    {
        panic_status(
            LUCID_FAIL_EX(
                1,
                "usermode enter failed",
                "entry address is not mapped",
                "check user code page"
            )
        );
    }

    usermode_drop(
        addr,
        USER_STACK_TOP,
        USER_CODE_SELECTOR,
        USER_DATA_SELECTOR
    );
}
