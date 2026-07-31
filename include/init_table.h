#ifndef LUCID_INIT_TABLE_H
#define LUCID_INIT_TABLE_H

#include "recipes/status.h"

/* One entry per recipe listed in INIT_BOOT (config.mk).
 * The actual array is generated at build time into .build/init_table.c
 * by build.sh -- see generate_init_table(). This header is the only
 * thing kernel.c needs to know about it. */

typedef lucid_status (*lucid_phase_fn)(void);

typedef struct {
    const char *name;
    lucid_phase_fn pre;
    lucid_phase_fn init;
    lucid_phase_fn post;
} lucid_init_entry;

extern lucid_init_entry init_table[];
extern const unsigned int init_table_count;

#endif
