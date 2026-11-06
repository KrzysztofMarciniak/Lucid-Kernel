# User-mode syscalls

Lucid installs the `int 0x80` handler in the `syscall` recipe. A call places
the syscall number in `EAX` and up to four arguments in `EBX`, `ECX`, `EDX`,
and `ESI`; the return value is in `EAX`. User pointers are checked against
user-mapped pages before the kernel reads or writes them. `0xFFFFFFFF` means
the requested operation failed. `exit` returns control to the kernel instead
of returning to the caller.

The public numbers and argument layouts are defined in
[`recipes/syscall.h`](recipes/syscall.h). The same list is readable at runtime
in `/system/info/syscalls`.

| Number | Name | Arguments | Result |
|---:|---|---|---|
| 0 | `SYS_WRITE` | `EBX` text pointer, `ECX` byte count | bytes written |
| 1 | `SYS_EXIT` | `EBX` exit status | does not return to user mode |
| 2 | `SYS_READLINE` | `EBX` output buffer, `ECX` capacity; optional `EDX` pointer to `sys_readline_completion`, `ESI` struct size (both zero for basic editing) | input length |
| 3 | `SYS_FS_READ` | `EBX` path, `ECX` path length, `EDX` output, `ESI` capacity | bytes read |
| 4 | `SYS_FS_WRITE` | `EBX` path, `ECX` path length, `EDX` input, `ESI` byte count | bytes written |
| 5 | `SYS_FS_LIST` | `EBX` path, `ECX` path length, `EDX` output, `ESI` capacity | bytes returned |
| 6 | `SYS_READKEY` | none | key code |
| 7 | `SYS_CLEAR` | none | zero |
| 8 | `SYS_SHUTDOWN` | none | requests machine poweroff |
| 9 | `SYS_GETTICKS` | none | PIT ticks since boot (100 ticks/second) |
| 10 | `SYS_FORK` | none | parent gets child PID; child resumes with result 0 |
| 11 | `SYS_EXEC` | `EBX` path pointer, `ECX` path length | replaces caller with a registered user image; does not return on success |
| 12 | `SYS_WAIT` | `EBX` child PID | runs that child synchronously until exit, then returns its PID |

Clearsh and other user programs can call `lucid_user_syscall()` from
`recipes/syscall.h` when compiled with `LUCID_USER`. Clearsh also provides a
checked `(syscall number ...)` form for the implemented calls. Arguments are
typed shell values: text for `SYS_WRITE`, file values for filesystem calls,
and no arguments for the other calls. Clearsh allocates user buffers itself;
scripts cannot supply raw pointers. `SYS_EXIT` is deliberately unavailable through the generic syscall form;
Clearsh exposes it as `(exit [status])`, after which userland Pioneer
restarts the shell. `SYS_EXEC` currently resolves paths from the kernel's
registered embedded-image table. `SYS_WAIT` is cooperative, not preemptive:
the caller blocks in the kernel while the child runs. See `SHELL.md` for
examples. Named functions such as `(uptime)`, `read`, `write`, and `print`
remain the simpler interfaces for common operations.

`SYS_READLINE` remains usable with only `EBX` and `ECX`. Clearsh passes the
optional `sys_readline_completion` structure in `EDX`/`ESI`; it contains the
current directory, prompt, and newline-separated function names. The kernel
validates the full structure before using it for Tab completion.

## Adding a syscall

1. Assign a new stable number and document its argument contract in
   `recipes/syscall.h`.
2. Add a dispatch branch in `impl/syscall/int80/syscall.c`. Validate every
   user pointer and its full byte range before dereferencing it; bound copies
   and return an explicit error value for invalid requests.
3. Add a narrow userspace wrapper or shell builtin if the operation should be
   available to Clearsh. Do not give shell expressions raw kernel pointers.
4. Update `/system/info/syscalls` in the RAMFS seed data and this reference.
5. Add a focused ring-3 test for success and invalid arguments.
