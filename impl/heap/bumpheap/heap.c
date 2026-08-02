#include "recipes/heap.h"
#include "recipes/vmm.h"
#include "recipes/status.h"
#include "panic.h"
#include "internal.h"
#include "stddef.h"

uint32_t heap_current = HEAP_START;
uint32_t heap_mapped_end = HEAP_START;


lucid_status heap_pre(void) {
    return LUCID_OK;
}

lucid_status heap_init(void) {
    return LUCID_OK;
}

lucid_status heap_post(void) {
    return LUCID_OK;
}

void *kmalloc(size_t size) {
    uint32_t aligned = (heap_current + 15) & ~15u;

    if (aligned + size > HEAP_START + HEAP_MAX_SIZE) {
        panic_status(LUCID_FAIL_EX(1, "kmalloc: heap exhausted",
            "requested allocation exceeds HEAP_MAX_SIZE",
            "increase HEAP_MAX_SIZE or free memory"));
    }

    uint32_t needed_end = aligned + size;

    while (heap_mapped_end < needed_end) {
        lucid_status st = vmm_map(heap_mapped_end, VMM_PRESENT | VMM_WRITABLE);
        if (st.ok != 0) {
            panic_status(st);
        }
        heap_mapped_end += VMM_PAGE_SIZE;
    }

    heap_current = needed_end;
    return (void *)aligned;
}
