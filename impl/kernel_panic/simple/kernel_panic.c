#include "recipes/kernel_panic.h"

lucid_status kernel_panic_pre(void) {
    return LUCID_OK;
}

lucid_status kernel_panic_init(void) {
    return LUCID_FAIL_EX(
        1,
        "Intentional kernel panic (test)",
        "kernel_panic recipe requested panic",
        "Remove 'kernel_panic' from config.mk to continue booting."
    );
}

lucid_status kernel_panic_post(void) {
    return LUCID_OK;
}
