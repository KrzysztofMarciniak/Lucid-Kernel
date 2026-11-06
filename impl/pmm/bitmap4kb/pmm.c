/*
 * pmm/simple4kb — bitmap physical frame allocator, 4KB frames, rotating
 * first-fit scan. No buddy allocator, no free-list, no NUMA. Frames below
 * the reserved boundary are never allocated.
 */

#include "recipes/pmm.h"
#include "recipes/status.h"
#include "internal.h"

uint8_t frame_bitmap[BITMAP_SIZE];
static uint32_t allocation_hint_frame;
static uint32_t free_frame_count;

static inline void bitmap_set(uint32_t frame) {
    frame_bitmap[frame / 8] |= (1 << (frame % 8));
}

static inline void bitmap_clear(uint32_t frame) {
    frame_bitmap[frame / 8] &= ~(1 << (frame % 8));
}

static inline int bitmap_test(uint32_t frame) {
    return frame_bitmap[frame / 8] & (1 << (frame % 8));
}

static uint32_t find_free_frame(uint32_t start, uint32_t end)
{
    while (start < end && (start & 7u)) {
        if (!bitmap_test(start)) return start;
        start++;
    }

    while (start + 8u <= end) {
        uint8_t bits = frame_bitmap[start / 8u];
        if (bits != 0xFFu) {
            for (uint32_t bit = 0; bit < 8; bit++) {
                if (!(bits & (uint8_t)(1u << bit))) {
                    return start + bit;
                }
            }
        }
        start += 8u;
    }

    while (start < end) {
        if (!bitmap_test(start)) return start;
        start++;
    }
    return TOTAL_FRAMES;
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
    allocation_hint_frame = reserved_frames;
    free_frame_count = TOTAL_FRAMES - reserved_frames;

    return LUCID_OK_MSG("PMM bitmap built, low 4MB reserved");
}

lucid_status pmm_post(void) {
    /* nothing to commit -- pmm has no CPU state to load, unlike
       gdt/idt. bitmap is ready for use as soon as _init finishes. */
    return LUCID_OK;
}

uint32_t pmm_alloc_frame(void) {
    if (free_frame_count == 0) return 0;

    uint32_t reserved_frames = RESERVED_BELOW / FRAME_SIZE;
    uint32_t frame = find_free_frame(allocation_hint_frame, TOTAL_FRAMES);
    if (frame == TOTAL_FRAMES) {
        frame = find_free_frame(reserved_frames, allocation_hint_frame);
    }
    if (frame == TOTAL_FRAMES) return 0;

    bitmap_set(frame);
    free_frame_count--;
    allocation_hint_frame = frame + 1;
    if (allocation_hint_frame == TOTAL_FRAMES) {
        allocation_hint_frame = reserved_frames;
    }
    return frame * FRAME_SIZE;
}

void pmm_free_frame(uint32_t addr) {
    uint32_t frame = addr / FRAME_SIZE;
    uint32_t reserved_frames = RESERVED_BELOW / FRAME_SIZE;
    if (frame >= reserved_frames && frame < TOTAL_FRAMES &&
        bitmap_test(frame)) {
        bitmap_clear(frame);
        free_frame_count++;
    }
}

void pmm_get_stats(uint32_t *managed_frames,
                   uint32_t *free_frames,
                   uint32_t *reserved_frames) {
    if (managed_frames) *managed_frames = TOTAL_FRAMES;
    if (free_frames) *free_frames = free_frame_count;
    if (reserved_frames) {
        *reserved_frames = RESERVED_BELOW / FRAME_SIZE;
    }
}
