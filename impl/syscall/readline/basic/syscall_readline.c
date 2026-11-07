#include "impl/syscall/internal.h"

SYSCALL_HANDLER(
    syscall_readline_entry, SYS_READLINE, syscall_readline_handle
);
