# User-mode syscalls

Lucid installs the `int 0x80` handler in the `syscall` recipe. A call places
the syscall number in `EAX` and up to four arguments in `EBX`, `ECX`, `EDX`,
and `ESI`; the return value is in `EAX`. User pointers are checked against
user-mapped pages before the kernel reads or writes them. `0xFFFFFFFF` means
the requested operation failed. `exit` returns control to the kernel instead
of returning to the caller.

Each public syscall number and argument contract lives in its own header,
such as [`recipes/syscall_write.h`](recipes/syscall_write.h). The
[`recipes/syscall.h`](recipes/syscall.h) umbrella includes these headers for
existing callers. The same list is readable at runtime in
`/system/info/syscalls`.

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
| 8 | `SYS_SHUTDOWN` | none | requests ACPI S5 poweroff, with emulator-port fallback |
| 9 | `SYS_GETTICKS` | none | PIT ticks since boot (100 ticks/second) |
| 10 | `SYS_FORK` | none | parent gets child PID; child resumes with result 0 |
| 11 | `SYS_EXEC` | `EBX` path pointer, `ECX` path length | replaces caller with a registered user image; does not return on success |
| 12 | `SYS_WAIT` | `EBX` child PID | blocks until that child exits, then returns its PID |
| 13 | `SYS_REAP` | none | releases exited child processes |
| 14 | `SYS_GET_TERMINAL_SIZE` | none | columns in high 16 bits, rows in low 16 bits |
| 15 | `SYS_FS_STAT` | `EBX` path, `ECX` path length | 1 for directory, 0 for file |

Clearsh and other user programs can call `lucid_user_syscall()` from
`recipes/syscall.h` when compiled with `LUCID_USER`. Clearsh also provides a
checked `(syscall number ...)` form for the implemented calls. Arguments are
typed shell values: text for `SYS_WRITE`, file values for filesystem calls,
and no arguments for the other calls. Clearsh allocates user buffers itself;
scripts cannot supply raw pointers. `SYS_EXIT` is deliberately unavailable through the generic syscall form;
Clearsh exposes it as `(exit [status])`. Brain manages Eyes as preemptively
scheduled processes, and the kernel reclaims exited children with
`SYS_REAP`. `SYS_EXEC` currently resolves paths from the kernel's registered
embedded-image table. See `SHELL.md` for examples. Named functions such as
`(uptime)`, `read`, `write`, and `print` remain the simpler interfaces for
common operations.

`SYS_READLINE` remains usable with only `EBX` and `ECX`. Clearsh passes the
optional `sys_readline_completion` structure in `EDX`/`ESI`; it contains the
current directory, prompt, and newline-separated function names. The kernel
validates the full structure before using it for Tab completion.

## Syscalls as files

The virtual `/system/syscalls` directory provides file-style access to common
kernel calls. List its endpoints with `(ls (file /system/syscalls))`. Reading
or writing an endpoint invokes its syscall; read results are plain text with a
trailing newline.

| Path | Operation | Behavior |
|---|---|---|
| `/system/syscalls/write` | write | Sends the supplied bytes to the terminal |
| `/system/syscalls/readkey` | read | Waits for a key and returns its numeric key code |
| `/system/syscalls/clear` | write | Clears the calling process's terminal |
| `/system/syscalls/shutdown` | write | Requests ACPI S5 poweroff, with emulator-port fallback |
| `/system/syscalls/get_ticks` | read | Returns PIT ticks since boot |
| `/system/syscalls/terminal_size` | read | Returns `columns rows` |
| `/system/syscalls/reap` | read | Reaps exited children and returns the count |

`/system/processes/list` is a read-only process table with PID, parent PID,
state, and image columns. Write an image path or unique image basename to
`/system/processes/kill` to terminate that process. The current process and
PID 1 cannot be killed; missing or ambiguous names fail.

For example, `(write (file /system/syscalls/write) (text "hello\n"))` writes
to the terminal, while `(print (read (file /system/syscalls/get_ticks)))`
returns the current tick count. Write-only endpoints reject reads; read-only
endpoints reject writes. These virtual nodes are not regular RAMFS files and
cannot be overwritten with file contents.

## Adding a syscall

1. Add `recipes/syscall_<name>.h` with a stable `SYS_<NAME>` number and a
   concise register/return contract. Include it from `recipes/syscall.h` to
   preserve the umbrella API, or include it directly from new callers.
2. Add an implementation such as
   `impl/syscall/<name>/basic/syscall_<name>.c`. Implement a handler taking a
   `syscall_frame *`, validate every user pointer and full byte range before
   dereferencing it, bound copies, and set an explicit error result for invalid
   requests. Register it with `SYSCALL_HANDLER(symbol, SYS_<NAME>, handler)`;
   the linker collects registered handlers, so no dispatch branch is needed.
3. Add the name to `SYSCALLS` and set `SYSCALL_<NAME>_IMPL=basic` in
   `config.mk`. The build verifies the contract and selected handler exist and
   compiles only the configured syscall modules; different calls can select
   different implementation directories.
4. Add a narrow typed userspace wrapper or shell builtin if the operation
   should be available to Clearsh. Do not give shell expressions raw kernel
   pointers.
5. Update `/system/info/syscalls` in the RAMFS seed data and this reference.
6. Add a focused ring-3 test for success and invalid arguments.

`SYS_READLINE` uses a registered handler, but its editing, history, and
completion helper implementation remains in `impl/syscall/int80/syscall.c`.
