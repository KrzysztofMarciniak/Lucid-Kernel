#ifndef LUCID_RECIPE_IDT_H
#define LUCID_RECIPE_IDT_H

#include "status.h"

lucid_status idt_pre(void);
lucid_status idt_init(void);
lucid_status idt_post(void);

#endif
