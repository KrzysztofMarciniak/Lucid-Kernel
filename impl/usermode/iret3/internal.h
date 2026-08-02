#ifndef LUCID_IMPL_USERMODE_IRET3_INTERNAL_H
#define LUCID_IMPL_USERMODE_IRET3_INTERNAL_H

#include <stdint.h>

/* GDT layout from impl/gdt/flat -- ring3 code/data selectors.
   Must match gdt's actual entry order: null, kcode, kdata, ucode,
   udata, TSS -- ring3 selectors are entries 3 and 4, |3 for RPL. */
#define USER_CODE_SELECTOR (0x18 | 3) /* entry 3 * 8 = 0x18, RPL 3 */
#define USER_DATA_SELECTOR (0x20 | 3) /* entry 4 * 8 = 0x20, RPL 3 */

/* fixed virtual range for the user stack -- one page, well above
   heap's range so it never collides as heap grows. */
#define USER_STACK_TOP  0x08000000  /* 128MB, arbitrary but out of the way */
#define USER_STACK_SIZE 4096

void usermode_drop(uint32_t entry, uint32_t user_stack, uint32_t code_sel, uint32_t data_sel);

#endif
