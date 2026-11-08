#ifndef LUCID_SYSCALL_POSIX_INTERNAL_H
#define LUCID_SYSCALL_POSIX_INTERNAL_H

#include "impl/syscall/internal.h"
#include "recipes/syscall_posix.h"
#include <errno.h>

#define POSIX_FD_LIMIT 16u
#define POSIX_FIRST_FILE_FD 3u
#define POSIX_FD_COUNT (POSIX_FD_LIMIT - POSIX_FIRST_FILE_FD)
#define POSIX_DESCRIPTION_COUNT \
    (USERMODE_PROCESS_SLOTS * POSIX_FD_COUNT)
#define POSIX_INVALID_DESCRIPTION 0xFFu

typedef enum {
    POSIX_FILE_OPEN,
    POSIX_FILE_ACCESS,
    POSIX_FILE_UNLINK,
    POSIX_FILE_CLOSE,
    POSIX_FILE_LSEEK,
    POSIX_FILE_WRITE
} posix_file_operation;

typedef struct {
    uint8_t initialized;
    uint8_t closed_standard;
    uint8_t descriptions[POSIX_FD_COUNT];
} posix_process_state;

typedef struct {
    uint16_t references;
    uint8_t used;
    uint16_t flags;
    uint32_t offset;
    char path[SYS_PATH_MAX];
} posix_open_description;

posix_process_state *syscall_posix_state(uint32_t slot);
posix_open_description *syscall_posix_description(uint32_t index);
posix_open_description *syscall_posix_get_description(uint32_t slot,
                                                       uint32_t fd);
uint32_t syscall_posix_current_slot(void);
int syscall_posix_find_free_descriptor(uint32_t slot);
int syscall_posix_find_free_description(void);
void syscall_posix_install_description(uint32_t slot,
                                       uint32_t fd,
                                       uint32_t description);
void syscall_posix_release_description(uint32_t index);
int32_t syscall_posix_error(int error);
int32_t syscall_posix_ramfs_error(int result);
int32_t syscall_posix_copy_user_path(uint32_t address,
                                     char path[SYS_PATH_MAX]);

void syscall_posix_process_fork(uint32_t parent_slot, uint32_t child_slot);
void syscall_posix_process_exit(uint32_t slot);
void syscall_posix_set_last_error(uint32_t slot, int32_t result);
int32_t syscall_posix_last_error(uint32_t slot);

uint32_t syscall_posix_open_handle(syscall_frame *frame);
uint32_t syscall_posix_read_handle(syscall_frame *frame);
uint32_t syscall_posix_write_handle(syscall_frame *frame);
uint32_t syscall_posix_close_handle(syscall_frame *frame);
uint32_t syscall_posix_lseek_handle(syscall_frame *frame);
uint32_t syscall_posix_unlink_handle(syscall_frame *frame);
uint32_t syscall_posix_access_handle(syscall_frame *frame);

#endif
