#ifndef LUCID_RECIPE_USERMODE_H
#define LUCID_RECIPE_USERMODE_H

/* REQUIRES: gdt, idt, vmm, heap */

#include "status.h"
#include <stddef.h>
#include <stdint.h>

#define USERMODE_STACK_TOP 0x00800000u
#define USERMODE_STACK_SIZE 0x00100000u

typedef struct {
    uint32_t gs;
    uint32_t fs;
    uint32_t es;
    uint32_t ds;
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t saved_esp;
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
    uint32_t user_esp;
    uint32_t user_ss;
} usermode_frame;

lucid_status usermode_pre(void);
lucid_status usermode_init(void);
lucid_status usermode_post(void);

/* After paging is enabled, map a flat image and zero-fill its memory region. */
lucid_status usermode_load_image(uint32_t base,
                                 const uint8_t *image,
                                 uint32_t image_size,
                                 uint32_t memory_size);
lucid_status usermode_reset_stack(void);

/* drops from ring0 to ring3, jumping to entry with a fresh user
   stack. Never returns to the caller directly -- control comes back
   via a syscall/interrupt trapping into ring0 (e.g. int 0x80). */
void usermode_enter(void (*entry)(void));
void usermode_resume_frame(usermode_frame *frame);

uint32_t usermode_kernel_stack_top(void);
void usermode_set_kernel_stack_top(uint32_t top);
uint32_t usermode_return_esp(void);
void usermode_set_return_esp(uint32_t esp);

uint32_t usermode_process_fork(usermode_frame *frame);
uint32_t usermode_process_wait(uint32_t pid);
int usermode_process_exec(const char *path, usermode_frame *frame);
void usermode_process_exit(uint32_t status);
int usermode_process_info(char *buffer, size_t capacity, size_t *length);

#endif
