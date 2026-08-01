#ifndef LUCID_RECIPE_IDT_H
#define LUCID_RECIPE_IDT_H

#include "status.h"

struct interrupt_frame;

lucid_status idt_pre(void);
lucid_status idt_init(void);
lucid_status idt_post(void);

/* install a handler for a specific vector, overriding the default.
   callable by any recipe running after idt in INIT_BOOT order. */
void idt_install_handler(int vector, void (*handler)(struct interrupt_frame *));

#endif
