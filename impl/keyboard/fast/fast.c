/* bike.c - keyboard :: bike
 * profile: minimal / small / fragile
 * No controller probing, no scancode translation table. Assumes a
 * keyboard is present and returns raw scancodes cast to char --
 * letters/numbers happen to be close to readable on set 1 for a
 * handful of keys, everything else is garbage. ~40 bytes .text.
 * pick this when: size budget is tight, input is a known-good
 * keyboard, and you don't need real ASCII (e.g. a debug REPL that
 * only cares about a few known raw codes).
 */
#include "recipes/keyboard.h"
#include "stdint.h"

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

#define PS2_DATA   0x60
#define PS2_STATUS 0x64

lucid_status keyboard_pre(void) {
    /* No check. If there's no controller, keyboard_read() spins
     * forever the first time it's called -- that's the tradeoff. */
    return LUCID_OK;
}

lucid_status keyboard_init(void) {
    return LUCID_OK;
}

lucid_status keyboard_post(void) {
    return LUCID_OK;
}

int keyboard_haskey(void) {
    return (inb(PS2_STATUS) & 0x01) ? 1 : 0;
}

char keyboard_read(void) {
    while (!keyboard_haskey()) { }
    return (char)inb(PS2_DATA);
}
