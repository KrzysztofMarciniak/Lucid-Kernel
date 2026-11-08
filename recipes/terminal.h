#ifndef LUCID_RECIPE_TERMINAL_H
#define LUCID_RECIPE_TERMINAL_H

#include "recipes/status.h"
#include <stddef.h>
#include "stdint.h"

struct multiboot_info;

#define TERMINAL_MAX_COLUMNS 170u
#define TERMINAL_MAX_ROWS 64u

/* Recipe: terminal
 * Implementations provide console output, clearing, and input. How output
 * is rendered remains implementation-private.
 */

#define TERMINAL_KEY_UP 0x11
#define TERMINAL_KEY_DOWN 0x12
#define TERMINAL_KEY_LEFT 0x13
#define TERMINAL_KEY_RIGHT 0x14
#define TERMINAL_KEY_DELETE 0x15
#define TERMINAL_KEY_HOME 0x16
#define TERMINAL_KEY_END 0x17
#define TERMINAL_KEY_CTRL_BASE 0x100
#define TERMINAL_KEY_CTRL_LEFT 0x120
#define TERMINAL_KEY_CTRL_RIGHT 0x121
#define TERMINAL_KEY_MOD_BASE 0x400
#define TERMINAL_KEY_MOD_SHIFT_BASE 0x500

lucid_status terminal_pre(void);
lucid_status terminal_init(void);
lucid_status terminal_post(void);

void terminal_set_boot_info(const struct multiboot_info *info);
int terminal_framebuffer_mapping(uint32_t *base, uint32_t *length);
uint32_t terminal_get_size(void);
void terminal_refresh(void);
void terminal_config_reload(void);
void terminal_statusbar_refresh(void);
void terminal_process_reset(uint32_t slot);
void terminal_set_status(const char *text);

void terminal_putc(char c);
void terminal_write(const char *s);
int terminal_screen_read(char *buffer, size_t capacity, size_t *length);
void terminal_flush(void);
void terminal_clear(void);
void terminal_cursor_show(void);
void terminal_cursor_hide(void);
int terminal_getchar(void);

#endif
