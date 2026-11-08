#include <errno.h>
#include "impl/syscall/posix/posix_internal.h"
#include "recipes/ramfs.h"

static posix_process_state process_states[USERMODE_PROCESS_SLOTS];
static posix_open_description descriptions[POSIX_DESCRIPTION_COUNT];

static void release_description(uint32_t index)
{
    if (index >= POSIX_DESCRIPTION_COUNT) return;
    posix_open_description *description = &descriptions[index];
    if (!description->used || description->references == 0) return;
    description->references--;
    if (description->references == 0) {
        description->used = 0;
        description->flags = 0;
        description->offset = 0;
        description->path[0] = '\0';
    }
}

posix_process_state *syscall_posix_state(uint32_t slot)
{
    if (slot >= USERMODE_PROCESS_SLOTS) return 0;
    posix_process_state *state = &process_states[slot];
    if (state->initialized) return state;
    for (uint32_t i = 0; i < POSIX_FD_COUNT; i++) {
        state->descriptions[i] = POSIX_INVALID_DESCRIPTION;
    }
    state->initialized = 1;
    return state;
}

void syscall_posix_process_exit(uint32_t slot)
{
    posix_process_state *state = syscall_posix_state(slot);
    if (!state) return;
    for (uint32_t i = 0; i < POSIX_FD_COUNT; i++) {
        uint8_t index = state->descriptions[i];
        if (index != POSIX_INVALID_DESCRIPTION) {
            release_description(index);
            state->descriptions[i] = POSIX_INVALID_DESCRIPTION;
        }
    }
    state->closed_standard = 0x07u;
}

void syscall_posix_process_fork(uint32_t parent_slot, uint32_t child_slot)
{
    posix_process_state *parent = syscall_posix_state(parent_slot);
    posix_process_state *child = syscall_posix_state(child_slot);
    if (!parent || !child) return;
    syscall_posix_process_exit(child_slot);
    child->closed_standard = parent->closed_standard;
    for (uint32_t i = 0; i < POSIX_FD_COUNT; i++) {
        uint8_t index = parent->descriptions[i];
        child->descriptions[i] = index;
        if (index != POSIX_INVALID_DESCRIPTION &&
            descriptions[index].used &&
            descriptions[index].references < 0xFFFFu) {
            descriptions[index].references++;
        }
    }
}

uint32_t syscall_posix_current_slot(void)
{
    uint32_t slot = usermode_process_current_slot();
    return slot < USERMODE_PROCESS_SLOTS ? slot : 0;
}

int syscall_posix_find_free_descriptor(uint32_t slot)
{
    posix_process_state *state = syscall_posix_state(slot);
    if (!state) return -1;
    for (uint32_t fd = POSIX_FIRST_FILE_FD; fd < POSIX_FD_LIMIT; fd++) {
        if (state->descriptions[fd - POSIX_FIRST_FILE_FD] ==
            POSIX_INVALID_DESCRIPTION) {
            return (int)fd;
        }
    }
    return -1;
}

int syscall_posix_find_free_description(void)
{
    for (uint32_t i = 0; i < POSIX_DESCRIPTION_COUNT; i++) {
        if (!descriptions[i].used) return (int)i;
    }
    return -1;
}

posix_open_description *syscall_posix_description(uint32_t index)
{
    return index < POSIX_DESCRIPTION_COUNT ? &descriptions[index] : 0;
}

posix_open_description *syscall_posix_get_description(uint32_t slot,
                                                       uint32_t fd)
{
    if (fd < POSIX_FIRST_FILE_FD || fd >= POSIX_FD_LIMIT) return 0;
    posix_process_state *state = syscall_posix_state(slot);
    if (!state) return 0;
    uint8_t index = state->descriptions[fd - POSIX_FIRST_FILE_FD];
    if (index == POSIX_INVALID_DESCRIPTION ||
        index >= POSIX_DESCRIPTION_COUNT ||
        !descriptions[index].used) {
        return 0;
    }
    return &descriptions[index];
}

void syscall_posix_install_description(uint32_t slot,
                                       uint32_t fd,
                                       uint32_t description)
{
    if (fd < POSIX_FIRST_FILE_FD || fd >= POSIX_FD_LIMIT ||
        description >= POSIX_DESCRIPTION_COUNT) {
        return;
    }
    posix_process_state *state = syscall_posix_state(slot);
    if (!state) return;
    state->descriptions[fd - POSIX_FIRST_FILE_FD] = (uint8_t)description;
}

void syscall_posix_release_description(uint32_t index)
{
    release_description(index);
}

int32_t syscall_posix_error(int error)
{
    return -(int32_t)error;
}

int32_t syscall_posix_ramfs_error(int result)
{
    switch (result) {
    case RAMFS_NOT_FOUND: return syscall_posix_error(ENOENT);
    case RAMFS_NOT_DIRECTORY: return syscall_posix_error(ENOTDIR);
    case RAMFS_TOO_LARGE: return syscall_posix_error(EFBIG);
    case RAMFS_INVALID_PATH: return syscall_posix_error(EINVAL);
    case RAMFS_NO_SPACE: return syscall_posix_error(ENOSPC);
    case RAMFS_READ_ONLY: return syscall_posix_error(EROFS);
    case RAMFS_IS_DIRECTORY: return syscall_posix_error(EISDIR);
    default: return syscall_posix_error(EIO);
    }
}

int32_t syscall_posix_copy_user_path(uint32_t address,
                                     char path[SYS_PATH_MAX])
{
    if (address == 0) return syscall_posix_error(EFAULT);
    for (uint32_t i = 0; i < SYS_PATH_MAX; i++) {
        if (address + i < address ||
            !syscall_user_range_valid(address + i, 1, 0)) {
            return syscall_posix_error(EFAULT);
        }
        path[i] = ((const char *)address)[i];
        if (path[i] == '\0') {
            return i == 0 ? syscall_posix_error(ENOENT) : 0;
        }
    }
    return syscall_posix_error(ENAMETOOLONG);
}
