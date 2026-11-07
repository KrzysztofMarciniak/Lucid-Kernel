#ifndef LUCID_SYSCALL_INT80_INTERNAL_H
#define LUCID_SYSCALL_INT80_INTERNAL_H

#include "recipes/syscall.h"
#include "recipes/usermode.h"

typedef usermode_frame syscall_frame;
typedef uint32_t (*syscall_handler_fn)(syscall_frame *frame);

typedef struct {
    uint32_t number;
    syscall_handler_fn handler;
} syscall_handler_entry;

typedef enum {
    SYSCALL_FILE_OUTSIDE,
    SYSCALL_FILE_DIRECTORY,
    SYSCALL_FILE_UNKNOWN,
    SYSCALL_FILE_WRITE,
    SYSCALL_FILE_READKEY,
    SYSCALL_FILE_CLEAR,
    SYSCALL_FILE_SHUTDOWN,
    SYSCALL_FILE_GET_TICKS,
    SYSCALL_FILE_TERMINAL_SIZE,
    SYSCALL_FILE_REAP
} syscall_file_kind;

#define SYSCALL_HANDLER(symbol, syscall_number, callback) \
    static const syscall_handler_entry symbol \
        __attribute__((section(".syscall_handlers"), used)) = { \
            (syscall_number), (callback) \
        }

int syscall_user_range_valid(uint32_t address,
                             uint32_t length,
                             int write);
uint32_t syscall_readline_handle(syscall_frame *frame);
void syscall_poweroff_system(void);
char *syscall_filesystem_buffer(void);
int syscall_text_equals(const char *left, const char *right);
syscall_file_kind syscall_file_lookup(const char *path);
int syscall_file_list(const char *path,
                      char *buffer,
                      size_t capacity,
                      size_t *length);
int syscall_file_read(const char *path,
                      char *buffer,
                      size_t capacity,
                      size_t *length);
int syscall_file_write(const char *path,
                       const char *data,
                       size_t length);

#endif
