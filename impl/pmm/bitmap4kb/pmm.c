/*
 * pmm/simple4kb — bitmap physical frame allocator, 4KB frames, linear
 * scan for the first free frame. No buddy allocator, no free-list,
 * no NUMA. First 1MB always reserved (kernel image, boot structures).
 * Upgrade only when perf/fragmentation actually demands it.
 */

#include "recipes/pmm.h"
#include "recipes/status.h"
#include "internal.h"

uint8_t frame_bitmap[BITMAP_SIZE];

static inline void bitmap_set(uint32_t frame) {
    frame_bitmap[frame / 8] |= (1 << (frame % 8));
}

static inline void bitmap_clear(uint32_t frame) {
    frame_bitmap[frame / 8] &= ~(1 << (frame % 8));
}

static inline int bitmap_test(uint32_t frame) {
    return frame_bitmap[frame / 8] & (1 << (frame % 8));
}

lucid_status pmm_pre(void) {
    return LUCID_OK;
}

lucid_status pmm_init(void) {
    for (uint32_t i = 0; i < BITMAP_SIZE; i++) {
        frame_bitmap[i] = 0; /* all free initially */
    }

    /* reserve everything below RESERVED_BELOW */
    uint32_t reserved_frames = RESERVED_BELOW / FRAME_SIZE;
    for (uint32_t i = 0; i < reserved_frames; i++) {
        bitmap_set(i);
    }

    return LUCID_OK_MSG("PMM bitmap built, low 1MB reserved");
}

lucid_status pmm_post(void) {
    /* nothing to commit -- pmm has no CPU state to load, unlike
       gdt/idt. bitmap is ready for use as soon as _init finishes. */
    return LUCID_OK;
}

uint32_t pmm_alloc_frame(void) {
    for (uint32_t i = 0; i < TOTAL_FRAMES; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            return i * FRAME_SIZE;
        }
    }
    return 0; /* out of memory */
}

void pmm_free_frame(uint32_t addr) {
    uint32_t frame = addr / FRAME_SIZE;
    if (frame < TOTAL_FRAMES) {
        bitmap_clear(frame);
    }
}
