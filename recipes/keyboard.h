#ifndef LUCID_RECIPE_KEYBOARD_H
#define LUCID_RECIPE_KEYBOARD_H

#include "recipes/status.h"

/* Recipe: keyboard
 * Any implementation in impl/keyboard/<codename>/ MUST define exactly
 * these symbols. Buffer size, scancode translation, controller
 * verification -- all impl-private. See each codename's doc header
 * for its specific tradeoffs.
 */

lucid_status keyboard_pre(void);
lucid_status keyboard_init(void);
lucid_status keyboard_post(void);

int  keyboard_haskey(void);   /* 1 if a key is buffered, 0 otherwise */
char keyboard_read(void);     /* blocks until a key is available */

#endif
