#include "impl/syscall/internal.h"
#include "recipes/timer.h"

static uint32_t syscall_getticks_handle(syscall_frame *frame)
{
    frame->eax = timer_get_ticks();
    return 0;
}

SYSCALL_HANDLER(
    syscall_getticks_entry, SYS_GETTICKS, syscall_getticks_handle
);
