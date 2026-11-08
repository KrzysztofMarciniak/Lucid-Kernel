#include "recipes/timer.h"
#include "recipes/idt.h"
#include "recipes/status.h"
#include "recipes/usermode.h"
#include "internal.h"

volatile uint32_t timer_ticks = 0;
static uint32_t clock_tick_phase;

extern void timer_isr(void);

uint32_t *timer_tick_handler(uint32_t *stack)
{
    clock_tick_phase += CLOCK_TICK_HZ;
    if (clock_tick_phase >= TIMER_IRQ_HZ) {
        clock_tick_phase -= TIMER_IRQ_HZ;
        timer_ticks++;
        usermode_process_tick(timer_ticks);
    }

    outb(0x20, 0x20);
    return usermode_process_schedule_interrupt(stack);
}

uint32_t timer_get_ticks(void)
{
    return timer_ticks;
}

lucid_status timer_pre(void)
{
    return LUCID_OK;
}

lucid_status timer_init(void)
{
    uint16_t divisor =
        (PIT_BASE_FREQ + TIMER_IRQ_HZ / 2) / TIMER_IRQ_HZ;

    outb(PIT_COMMAND, 0x36);
    outb(PIT_CHANNEL0, divisor & 0xFF);
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);

    return LUCID_OK_MSG("PIT programmed (250 Hz)");
}

lucid_status timer_post(void)
{
    idt_install_handler(32, (uint32_t)timer_isr);

    uint8_t mask = inb(0x21);
    outb(0x21, mask & ~0x01);

    return LUCID_OK_MSG("Timer IRQ0 unmasked, scheduling at 250 Hz");
}
