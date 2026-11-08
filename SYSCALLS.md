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

## Examples

User programs normally use the libc wrappers rather than issuing `int 0x80`
themselves. For example, `/system/info/version` contains the version string:

```c
#include <lucid.h>
#include "recipes/syscall_abi.h"

char version[32];
uint32_t length = lucid_fs_read(
    "/system/info/version", version, sizeof(version) - 1
);
if (length == SYS_UNKNOWN_TEST) {
    /* The read failed. */
} else {
    version[length] = '\0';
    /* Use version, for example "0.1.0-pre\n". */
}
```

Writing a regular RAMFS file creates it if its parent directory exists. The
returned count is the number of bytes written; the buffer is not NUL-terminated
by the syscall:

```c
#include <lucid.h>
#include "recipes/syscall_abi.h"

static const char path[] = "/tmp/syscall-example";
static const char message[] = "written through syscall 4\n";

uint32_t written = lucid_fs_write(path, message, sizeof(message) - 1);
if (written == SYS_UNKNOWN_TEST) {
    /* The write failed. */
} else {
    char contents[64];
    uint32_t length = lucid_fs_read(path, contents, sizeof(contents) - 1);
    if (length == SYS_UNKNOWN_TEST) {
        /* The read failed. */
    } else {
        contents[length] = '\0';
        /* contents now holds the file data as a C string. */
    }
}
```

A read from a virtual endpoint can also perform an operation. This blocks
until 18 timer ticks have elapsed; endpoint reads that take no input use a
zero capacity:

```c
char unused;
uint32_t result = lucid_fs_read("/system/time/sleep/18", &unused, 0);
if (result == SYS_UNKNOWN_TEST) {
    /* The sleep request failed. */
}
```

`recipes/syscall.h` provides wrappers for common endpoints. For example,
`lucid_get_ticks()` returns the tick count, `lucid_sleep(18)` waits 18 ticks,
and `lucid_fs_is_directory()` returns `1` for a directory, `0` for a file, or
`-1` on error. To wait for a file change, `lucid_fs_watch(path)` blocks until
that existing mutable file is written or unlinked, then returns `1`:

```c
#include "recipes/syscall.h"

uint32_t ticks = lucid_get_ticks();
uint32_t waited = lucid_sleep(18);
int is_directory = lucid_fs_is_directory("/system");
uint32_t changed = lucid_fs_watch("/tmp/syscall-example");
```

For code using POSIX-style descriptors, the libc handles the endpoint
marshalling and converts negative kernel errno results to `-1` and `errno`:

```c
#include <fcntl.h>
#include <unistd.h>

char version[32];
int fd = open("/system/info/version", O_RDONLY);
if (fd >= 0) {
    ssize_t length = read(fd, version, sizeof(version));
    if (length >= 0) {
        /* Use version[0..length); read does not append a NUL byte. */
    }
    (void)close(fd);
}
```

## Clearsh examples

Clearsh's `read` and `write` functions use the same filesystem calls. A
Clearsh session can read a virtual file, create/read a regular file, or use
the checked syscall form directly:

```lisp
(print (read (file /system/info/version)))
(write (file /tmp/note) (text "saved from Clearsh\n"))
(print (read (file /tmp/note)))
(print (syscall 3 (file /system/syscalls/get_ticks)))
(syscall 4 (file /tmp/note) (text "updated through syscall 4\n"))
```

In `(syscall number ...)`, syscall `3` takes one `(file ...)` argument and
returns its contents as text. Syscall `4` takes a `(file ...)` and a `(text
...)` value and returns the number of bytes written. Only these two syscall
numbers are accepted; the shell does not expose raw pointers or register
arguments. System operations such as sleeping are used by reading their
virtual endpoint, for example:

```lisp
(print (read (file /system/time/sleep/18)))
```

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
