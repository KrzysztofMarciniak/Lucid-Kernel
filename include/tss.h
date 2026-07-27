#ifndef TSS_H
#define TSS_H
#include <stdint.h>

void tss_install(int gdt_index, uint16_t kernel_ss, uint32_t kernel_esp0);
void tss_set_kernel_stack(uint32_t esp0);

#endif
