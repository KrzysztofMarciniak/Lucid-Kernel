#ifndef LUCID_IMPL_PMM_BITMAP4KB_INTERNAL_H
#define LUCID_IMPL_PMM_BITMAP4KB_INTERNAL_H

#include <stdint.h>

#define FRAME_SIZE 4096

#ifndef MEMORY_MB
#define MEMORY_MB 256  /* fallback if config.mk didn't define it */
#endif

#ifndef LOW_RESERVED_MB
#define LOW_RESERVED_MB 4  /* fallback if config.mk didn't define it */
#endif

#define TOTAL_FRAMES ((MEMORY_MB * 1024 * 1024) / FRAME_SIZE)
#define BITMAP_SIZE  (TOTAL_FRAMES / 8)

/*
 * Frames below this physical address are always reserved.
 * This protects the bootstrap/kernel identity-mapped region.
 */
#define RESERVED_BELOW (LOW_RESERVED_MB * 1024 * 1024)

extern uint8_t frame_bitmap[BITMAP_SIZE];

#endif
