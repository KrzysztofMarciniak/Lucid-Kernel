#ifndef LUCID_RECIPE_TERMINAL_H
#define LUCID_RECIPE_TERMINAL_H

#include "recipes/status.h"

/* Recipe: terminal
 * Any implementation in impl/terminal/<codename>/ MUST define exactly
 * these symbols. How output is actually rendered (VGA text buffer,
 * serial passthrough, framebuffer, ...) is entirely impl-private.
 */

lucid_status terminal_pre(void);
lucid_status terminal_init(void);
lucid_status terminal_post(void);

void terminal_putc(char c);
void terminal_write(const char *s);

#endif
