#include "impl/syscall/posix/posix_internal.h"

static int32_t posix_close_file(uint32_t fd, uint32_t slot)
{
    posix_process_state *state = syscall_posix_state(slot);
    if (fd < POSIX_FIRST_FILE_FD) {
        if (fd >= 3 || (state->closed_standard & (1u << fd)) != 0) {
            return syscall_posix_error(EBADF);
        }
        state->closed_standard |= (uint8_t)(1u << fd);
        return 0;
    }
    if (fd >= POSIX_FD_LIMIT) return syscall_posix_error(EBADF);

    uint32_t index = fd - POSIX_FIRST_FILE_FD;
    uint8_t description = state->descriptions[index];
    if (description == POSIX_INVALID_DESCRIPTION ||
        description >= POSIX_DESCRIPTION_COUNT ||
        !syscall_posix_description(description)->used) {
        return syscall_posix_error(EBADF);
    }
    state->descriptions[index] = POSIX_INVALID_DESCRIPTION;
    syscall_posix_release_description(description);
    return 0;
}

static uint32_t syscall_posix_close_handle(syscall_frame *frame)
{
    frame->eax = (uint32_t)posix_close_file(
        frame->ebx, syscall_posix_current_slot()
    );
    return 0;
}

SYSCALL_HANDLER(syscall_posix_close_entry,
                SYSCALL_POSIX_CLOSE,
                syscall_posix_close_handle);
