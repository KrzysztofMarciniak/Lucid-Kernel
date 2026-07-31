#ifndef GDT_DEFS_H
#define GDT_DEFS_H

#define SEG_PRESENT     0x80
#define SEG_DPL0        0x00
#define SEG_DPL3        0x60
#define SEG_S_NORMAL    0x10
#define SEG_EXEC        0x08
#define SEG_RW          0x02
#define SEG_TSS_TYPE    0x09
#define GRAN_4K_32BIT   0xC0

#define SEL(index, rpl) (((index) << 3) | (rpl))
#define SEL_KERNEL_CODE SEL(1, 0)  /* 0x08 -- matches idt.c/isr.s literals */
#define SEL_KERNEL_DATA SEL(2, 0)  /* 0x10 -- matches idt.c/isr.s literals */
#define SEL_USER_CODE   SEL(3, 3)
#define SEL_USER_DATA   SEL(4, 3)
#define SEL_TSS         SEL(5, 0)

#endif
