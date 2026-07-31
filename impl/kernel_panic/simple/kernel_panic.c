#include "recipes/kernel_panic.h"

lucid_status kernel_panic_pre(void) {
    return (lucid_status) {
        .ok = 0
    };
}

lucid_status kernel_panic_init(void) {
    return (lucid_status) {
        .ok = -1,
        .msg = "Kernel Panic Test Successful",
        .debug = "Kernel Panic test debug message",
        .hint = "Remove Kernel Panic from config.mk"
    };
}

lucid_status kernel_panic_post(void) {
    return (lucid_status) {
        .ok = 0
    };
}
