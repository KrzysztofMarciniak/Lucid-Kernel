/*
 * pic/simple8259 — simple 8259 PIC remap. Moves IRQ0-15 from vectors
 * 0-15 (which collide with CPU exceptions) to 32-47. Masks all lines
 * by default; a future recipe (timer, keyboard) unmasks its own line
 * when it comes online.
 */

#include "recipes/pic.h"
#include "recipes/status.h"
#include "internal.h"

lucid_status pic_pre(void) {
    return LUCID_OK;
}

lucid_status pic_init(void) {
    return LUCID_OK;
}

lucid_status pic_post(void) {
    outb(PIC1_CMD, 0x11); io_wait();
    outb(PIC2_CMD, 0x11); io_wait();

    outb(PIC1_DATA, PIC1_OFFSET); io_wait();
    outb(PIC2_DATA, PIC2_OFFSET); io_wait();

    outb(PIC1_DATA, 0x04); io_wait();
    outb(PIC2_DATA, 0x02); io_wait();

    outb(PIC1_DATA, 0x01); io_wait();
    outb(PIC2_DATA, 0x01); io_wait();

    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);

    return LUCID_OK_MSG("PIC remapped (IRQ0-15 -> vectors 32-47), all lines masked");
}
