#include "impl/syscall/internal.h"
#include "recipes/timer.h"
#include "recipes/usermode.h"

static uint32_t syscall_sleep_handle(syscall_frame *frame)
{
    uint32_t ticks = frame->ebx;
    if (ticks) {
        uint32_t start = timer_get_ticks();
        usermode_process_set_waiting_input(1);
        while ((uint32_t)(timer_get_ticks() - start) < ticks) {
            __asm__ volatile ("sti; hlt" ::: "memory");
        }
        usermode_process_set_waiting_input(0);
    }
    frame->eax = 0;
    return 0;
}

SYSCALL_HANDLER(syscall_sleep_entry, SYS_SLEEP, syscall_sleep_handle);
