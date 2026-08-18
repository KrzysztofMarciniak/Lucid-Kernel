#include "recipes/usermode_test.h"

#include "recipes/usermode.h"
#include "recipes/status.h"
#include "recipes/vmm.h"

#include "panic.h"

#include <stdint.h>


#define USER_TEST_CODE 0x00400000


/*
 * Ring3 payload:
 *
 *   cli
 *   hlt
 *   jmp $
 *
 * Expected behavior:
 * - CPU enters CPL3
 * - executes user instructions
 * - never returns
 *
 * Later replace this with:
 *
 *   mov eax, syscall_number
 *   int 0x80
 *
 * to test kernel entry.
 */

static const uint8_t ring3_payload[] =
{
    0xeb, 0xfe
};

lucid_status usermode_test_pre(void)
{
    return LUCID_OK;
}


lucid_status usermode_test_init(void)
{
    lucid_status st;


    st = vmm_map(
        USER_TEST_CODE,
        VMM_PRESENT |
        VMM_WRITABLE |
        VMM_USER
    );

    if (st.ok != 0)
    {
        return st;
    }


    for (uint32_t i = 0; i < sizeof(ring3_payload); i++)
    {
        ((uint8_t *)USER_TEST_CODE)[i] = ring3_payload[i];
    }


    return LUCID_OK_MSG(
        "ring3 entry test loaded"
    );
}


lucid_status usermode_test_post(void)
{
    return LUCID_FAIL_EX(
        1,
        "usermode test failed",
        "returned from ring3",
        "unexpected iret return"
    );
}


void usermode_test_run(void)
{
    usermode_enter(
        (void *)USER_TEST_CODE
    );
}
