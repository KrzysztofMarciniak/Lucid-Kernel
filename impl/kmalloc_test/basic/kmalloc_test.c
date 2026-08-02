/*
 * kmalloc_test/basic — sanity check for kmalloc, run once at boot.
 * Allocates a few chunks, checks pointers are non-NULL, distinct,
 * and 16-byte aligned, and that writes/reads through them work.
 * Throwaway diagnostic recipe -- remove from INIT_BOOT once trusted.
 */

#include "recipes/kmalloc_test.h"
#include "recipes/heap.h"
#include "recipes/status.h"

lucid_status kmalloc_test_pre(void) {
    return LUCID_OK;
}

lucid_status kmalloc_test_init(void) {
    void *a = kmalloc(32);
    void *b = kmalloc(64);
    void *c = kmalloc(4096); /* forces a page-growth in heap */

    if (!a || !b || !c) {
        return LUCID_FAIL_EX(1, "kmalloc_test failed", "kmalloc returned NULL", "check heap/vmm/pmm wiring");
    }

    if (a == b || b == c || a == c) {
        return LUCID_FAIL_EX(1, "kmalloc_test failed", "kmalloc returned overlapping/duplicate pointers", "check bump pointer logic in heap.c");
    }

    if (((unsigned)a % 16) || ((unsigned)b % 16) || ((unsigned)c % 16)) {
        return LUCID_FAIL_EX(1, "kmalloc_test failed", "returned pointer not 16-byte aligned", "check alignment math in kmalloc");
    }

    /* write/readback through each pointer */
    *(unsigned char *)a = 0xAA;
    *(unsigned char *)b = 0xBB;
    *(unsigned char *)c = 0xCC;

    if (*(unsigned char *)a != 0xAA || *(unsigned char *)b != 0xBB || *(unsigned char *)c != 0xCC) {
        return LUCID_FAIL_EX(1, "kmalloc_test failed", "write/readback mismatch", "check page mapping in vmm_map or pmm frame allocation");
    }

    return LUCID_OK_MSG("kmalloc test passed (3 allocations, distinct, aligned, read/write verified)");
}

lucid_status kmalloc_test_post(void) {
    return LUCID_OK;
}
