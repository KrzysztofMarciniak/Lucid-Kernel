#include "impl/syscall/internal.h"
#include "recipes/terminal.h"

static uint32_t syscall_write_handle(syscall_frame *frame)
{
    if (frame->ecx > SYS_WRITE_MAX ||
        !syscall_user_range_valid(frame->ebx, frame->ecx, 0)) {
        frame->eax = SYS_UNKNOWN_TEST;
        return 0;
    }

    const char *text = (const char *)frame->ebx;
    for (uint32_t i = 0; i < frame->ecx; i++) {
        terminal_putc(text[i]);
    }
    frame->eax = frame->ecx;
#ifdef LUCID_TEST_MODE
    __asm__ volatile ("outl %0, $0xF4" : : "a"(0x10u));
#endif
    return 0;
}

SYSCALL_HANDLER(syscall_write_entry, SYS_WRITE, syscall_write_handle);
