/*
 * timer/pit — 8253/8254 PIT on IRQ0, fixed 100 Hz. Programs the
 * divisor in _init, unmasks IRQ0 on the PIC and installs the ISR in
 * _post (the point interrupts can actually start firing). No
 * scheduling, no callbacks -- just a tick counter. First real proof
 * the IDT/PIC chain works end to end.
 */

#include "recipes/timer.h"
#include "recipes/idt.h"
#include "recipes/status.h"
#include "internal.h"

volatile uint32_t timer_ticks = 0;

__attribute__((interrupt))
static void timer_isr(struct interrupt_frame *frame) {
    (void)frame;
    timer_ticks++;
    outb(0x20, 0x20); /* PIC EOI */
}

lucid_status timer_pre(void) {
    return LUCID_OK;
}

lucid_status timer_init(void) {
    uint16_t divisor = PIT_BASE_FREQ / TIMER_HZ;

    outb(PIT_COMMAND, 0x36); /* channel 0, lobyte/hibyte, square wave */
    outb(PIT_CHANNEL0, divisor & 0xFF);
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);

    return LUCID_OK_MSG("PIT programmed (100 Hz)");
}

lucid_status timer_post(void) {
    idt_install_handler(32, timer_isr); /* IRQ0 -> vector 32, per pic remap */

    uint8_t mask = inb(0x21);
    outb(0x21, mask & ~0x01); /* unmask IRQ0 only */

    return LUCID_OK_MSG("Timer IRQ0 unmasked, ticking at 100 Hz");
}
