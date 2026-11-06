#include "recipes/usermode_test.h"

#include "recipes/usermode.h"
#include "recipes/status.h"
#include "recipes/syscall.h"

#include "panic.h"

#include <stdint.h>


#define USER_TEST_CODE 0x00400000
#define USER_TEST_MESSAGE_OFFSET 64
#define USER_TEST_MESSAGE (USER_TEST_CODE + USER_TEST_MESSAGE_OFFSET)
#define USER_TEST_MESSAGE_LENGTH (sizeof(ring3_message) - 1)
#define USER_TEST_IMAGE_SIZE (USER_TEST_MESSAGE_OFFSET + USER_TEST_MESSAGE_LENGTH)
#define BYTE0(value) ((value) & 0xFF)
#define BYTE1(value) (((value) >> 8) & 0xFF)
#define BYTE2(value) (((value) >> 16) & 0xFF)
#define BYTE3(value) (((value) >> 24) & 0xFF)

static const char ring3_message[] = "syscall write passed\n";
static const uint8_t ring3_payload[] =
{
    0xB8, BYTE0(SYS_WRITE), BYTE1(SYS_WRITE), BYTE2(SYS_WRITE), BYTE3(SYS_WRITE),
    0xBB, BYTE0(USER_TEST_MESSAGE), BYTE1(USER_TEST_MESSAGE),
    BYTE2(USER_TEST_MESSAGE), BYTE3(USER_TEST_MESSAGE),
    0xB9, BYTE0(USER_TEST_MESSAGE_LENGTH), BYTE1(USER_TEST_MESSAGE_LENGTH),
    BYTE2(USER_TEST_MESSAGE_LENGTH), BYTE3(USER_TEST_MESSAGE_LENGTH),
    0xCD, 0x80,
    0x3D, BYTE0(USER_TEST_MESSAGE_LENGTH), BYTE1(USER_TEST_MESSAGE_LENGTH),
    BYTE2(USER_TEST_MESSAGE_LENGTH), BYTE3(USER_TEST_MESSAGE_LENGTH),
    0x75, 0x15,
    0xB8, BYTE0(SYS_UNKNOWN_TEST), BYTE1(SYS_UNKNOWN_TEST),
    BYTE2(SYS_UNKNOWN_TEST), BYTE3(SYS_UNKNOWN_TEST),
    0xCD, 0x80,
    0x3D, 0xFF, 0xFF, 0xFF, 0xFF,
    0x75, 0x07,
    0xB8, BYTE0(SYS_EXIT), BYTE1(SYS_EXIT), BYTE2(SYS_EXIT), BYTE3(SYS_EXIT),
    0xCD, 0x80,
    0xCC
};

lucid_status usermode_test_pre(void)
{
    return LUCID_OK;
}


lucid_status usermode_test_init(void)
{
    return LUCID_OK;
}

static lucid_status load_test_image(void)
{
    uint8_t image[USER_TEST_IMAGE_SIZE];
    for (uint32_t i = 0; i < sizeof(image); i++) image[i] = 0;
    for (uint32_t i = 0; i < sizeof(ring3_payload); i++) {
        image[i] = ring3_payload[i];
    }
    for (uint32_t i = 0; i < USER_TEST_MESSAGE_LENGTH; i++) {
        image[USER_TEST_MESSAGE_OFFSET + i] = (uint8_t)ring3_message[i];
    }

    lucid_status st = usermode_load_image(
        USER_TEST_CODE,
        image,
        sizeof(image),
        sizeof(image)
    );
    if (st.ok != 0) return st;
    return LUCID_OK_MSG("ring3 entry test loaded");
}


void usermode_test_run(void)
{
    usermode_enter(
        (void *)USER_TEST_CODE
    );
}


lucid_status usermode_test_post(void)
{
    lucid_status st = load_test_image();
    if (st.ok != 0) return st;
    usermode_test_run();

    return LUCID_OK_MSG(
        "ring3 syscall ABI and exit passed"
    );
}
