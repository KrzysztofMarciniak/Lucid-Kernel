/*
 * usermode/iret3 — drops CPU privilege from ring0 to ring3 via a
 * manually built iret frame. One-shot: maps a single user stack
 * page, builds the frame, iret's into entry. No process table, no
 * scheduler -- just proves the mechanism works. Return to ring0
 * happens via whatever interrupt/syscall the entry point triggers.
 */

#include "recipes/usermode.h"
#include "recipes/vmm.h"
#include "recipes/status.h"
#include "internal.h"

lucid_status usermode_pre(void) {
    return LUCID_OK;
}

lucid_status usermode_init(void) {
    lucid_status st = vmm_map(USER_STACK_TOP - USER_STACK_SIZE, VMM_PRESENT | VMM_WRITABLE | VMM_USER);
    if (st.ok != 0) {
        return st;
    }
    return LUCID_OK_MSG("User stack mapped (1 page, ring3 accessible)");
}

lucid_status usermode_post(void) {
    return LUCID_OK;
}

void usermode_enter(void (*entry)(void)) {
    usermode_drop((uint32_t)entry, USER_STACK_TOP, USER_CODE_SELECTOR, USER_DATA_SELECTOR);
}
