#ifndef LUCID_RECIPE_GDT_H
#define LUCID_RECIPE_GDT_H

#include "recipes/status.h"
#include "stdint.h"

/* Recipe: gdt
 * Any implementation in impl/gdt/<codename>/ MUST define exactly these
 * symbols, with exactly these signatures. Nothing else about the impl
 * is guaranteed -- table size, TSS presence, whether it's segmented,
 * are all impl-private.
 *
 * Phase contract:
 *   gdt_pre   -- readiness check only, no side effects on failure.
 *   gdt_init  -- build the table, do NOT load it yet (lgdt/far-jump
 *                happens in _post, after every recipe's _pre passed).
 *   gdt_post  -- load the table (lgdt), reload segment registers.
 *
 * gdt_set_kernel_stack is not part of the tri-phase lifecycle -- it's
 * called later, whenever the scheduler needs to point ring0 entry at
 * a new stack. Every impl must provide it even if it's a no-op.
 */

lucid_status gdt_pre(void);
lucid_status gdt_init(void);
lucid_status gdt_post(void);

void gdt_set_kernel_stack(uint32_t esp0);

#endif
