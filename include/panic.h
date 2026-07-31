#ifndef LUCID_PANIC_H
#define LUCID_PANIC_H

/* Halts the CPU permanently. Never returns.
 * msg may be NULL (LUCID_OK never reaches here, but be defensive). */
__attribute__((noreturn)) void panic(const char *msg);
void panic_ok(const char *msg);


#endif
