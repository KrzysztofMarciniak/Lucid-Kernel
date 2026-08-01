#ifndef LUCID_IMPL_TIMER_PIT_100HZ_INTERNAL_H
#define LUCID_IMPL_TIMER_PIT_100HZ_INTERNAL_H

#include <stdint.h>

#define PIT_CHANNEL0 0x40
#define PIT_COMMAND  0x43

#define PIT_BASE_FREQ 1193182
#define TIMER_HZ      100  /* 100 ticks/sec, simple round number */

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

extern volatile uint32_t timer_ticks;

#endif
