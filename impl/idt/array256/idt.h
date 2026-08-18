#ifndef LUCID_RECIPE_IDT_H
#define LUCID_RECIPE_IDT_H

#include "status.h"

lucid_status idt_pre(void);
lucid_status idt_init(void);
lucid_status idt_post(void);

/* install a handler for a specific vector, overriding the default.
   handler_addr is the raw address of an asm entry point (must end
   in iret and follow the same stack-frame convention as isr.s). */
void idt_install_handler(int vector, uint32_t handler_addr);

#endif
