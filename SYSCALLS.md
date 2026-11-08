# User-mode I/O ABI

Lucid exposes two numbered system calls. Both use `int 0x80`; the call number
is in `EAX`, arguments are in `EBX`, `ECX`, `EDX`, and `ESI`, and the result is
returned in `EAX`. User pointers are validated before access. Ordinary I/O
failures return `0xFFFFFFFF`; POSIX descriptor endpoints return negative
errno values.

| Number | Name | Arguments | Result |
|---:|---|---|---|
| 3 | `SYS_FS_READ` | `EBX` path, `ECX` path length, `EDX` output buffer, `ESI` capacity | bytes read |
| 4 | `SYS_FS_WRITE` | `EBX` path, `ECX` path length, `EDX` input buffer, `ESI` byte count | bytes written |

All other kernel operations are exposed as virtual files. This removes their
individual syscall numbers, not the kernel functionality behind the files.
The paths below are virtual endpoints and are not regular RAMFS files.

## System and terminal files

| Path | Access | Behavior |
|---|---|---|
| `/system/syscalls/readkey` | read | Wait for a key and return its numeric code |
| `/system/syscalls/shutdown` | write | Request ACPI S5 poweroff, with emulator-port fallback |
| `/system/syscalls/get_ticks` | read | Return PIT ticks since boot |
| `/system/syscalls/terminal_size` | read | Return `columns rows` |
| `/system/syscalls/reap` | read | Reap exited children and return the count |
| `/system/terminals/current` | read/write | Read the calling process's terminal screen; a write replaces it and an empty write clears it |
| `/system/terminals/readline` | read | Read an edited input line |
| `/system/terminals/readline_config` | write | Set the prompt, current directory, and completion names for readline |

`/system/processes/list` reads the process table. Write an image path or unique
image basename to `/system/processes/kill` to terminate a process; the current
process and PID 1 cannot be killed. The current process endpoints are:

| Path | Access | Behavior |
|---|---|---|
| `/system/processes/current/fork` | read | Fork; return the child PID to the parent and zero to the child |
| `/system/processes/current/exec` | write | Replace the process with a registered embedded image |
| `/system/processes/current/exit` | write | Exit with the supplied decimal status |
| `/system/processes/current/wait/<pid>` | read | Wait for a child to exit and return its PID |
| `/system/processes/current/cwd` | read | Return the current directory (`/`) |

## Files and timing

| Path | Access | Behavior |
|---|---|---|
| `/system/files/type` | write, then read | Write a path; read `1` for a directory or `0` for a file |
| `/system/files/watch` | write, then read | Write an existing mutable RAMFS path; the read blocks until it is written or unlinked, then returns `1` |
| `/system/files/unlink` | write | Unlink the NUL-terminated path |
| `/system/time/wait/<ticks>` | read | Block until the requested ticks elapse and return the current tick count |
| `/system/time/sleep/<ticks>` | read | Block for the requested ticks and return zero |

File watches are one-shot and wake all processes waiting on that path after a
successful write or unlink. Call the endpoint again to wait for another
change.

## POSIX compatibility files

The libc compatibility layer keeps descriptors and offsets in the kernel.
`/system/posix/fd/<fd>` reads from or writes to a descriptor. Other operations
use `/system/posix` request files:

| Path | Access | Request/result |
|---|---|---|
| `/system/posix/open` | write | Four little-endian flag bytes followed by a NUL-terminated path; returns the descriptor or a negative errno |
| `/system/posix/access` | write | Four little-endian mode bytes followed by a NUL-terminated path; returns zero or a negative errno |
| `/system/posix/unlink` | write | NUL-terminated path; returns zero or a negative errno |
| `/system/posix/close` | write | Decimal descriptor; returns zero or a negative errno |
| `/system/posix/lseek` | write | `fd offset whence`; returns the new offset or a negative errno |
| `/system/posix/cwd` | read | Return `/` |
| `/system/posix/error` | read | Return the last POSIX error number |

These are a small compatibility subset, not a complete POSIX implementation.
Relative paths are rooted at `/`; permissions and `chdir` are not implemented.
Standard output and error write to the terminal, while standard input reads are
unsupported.

## Userland and extension

The small user-mode libc exposes `lucid_fs_read()` and `lucid_fs_write()` in
`<lucid.h>`, plus POSIX-style `open`, `read`, `write`, `close`, and related
functions from `<fcntl.h>` and `<unistd.h>`. These calls communicate with the
kernel through virtual-file endpoints; the raw `int 0x80` transport is confined
to the libc. `recipes/syscall.h` adds helpers for common system endpoints.
Clearsh's `(read ...)` and `(write ...)` use the filesystem calls. The checked
`(syscall number ...)` form accepts only syscall numbers 3 and 4.

Window-manager events `0x300`–`0x302` are delivered through key input; they are
not numbered syscalls.

To add a kernel operation, prefer adding a virtual file endpoint to
`impl/syscall/int80/syscall_files.c` and documenting its path, access mode,
request format, and result here. Add a numbered syscall only when the operation
cannot be expressed cleanly as file I/O.
