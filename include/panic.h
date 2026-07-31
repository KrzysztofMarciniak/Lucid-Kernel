#ifndef LUCID_PANIC_H
#define LUCID_PANIC_H

#include "recipes/status.h"

/* Halts the CPU permanently. Never returns. Prints msg always,
 * debug/hint only if the status set them (NULL = skip). */
__attribute__((noreturn)) void panic_status(lucid_status st);
void panic_ok(const char *msg);

/* Convenience for call sites that just have a raw string, no
 * lucid_status (e.g. asserts written before a recipe call exists). */
__attribute__((noreturn)) void panic(const char *msg);

#endif
