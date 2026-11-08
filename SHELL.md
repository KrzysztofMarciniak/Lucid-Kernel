# Clearsh

Clearsh is Lucid Kernel's foreground, ring-3 Lisp shell. Userland Pioneer
starts it with `fork`/`exec` and waits for it to exit, then restarts it. It is
available through both COM1 serial and the VGA text console. The filesystem is
currently RAM-backed. When booted from the provided disk image it saves
checksummed snapshots to the Lucid disk partition; without that partition,
writes are volatile.

The prompt shows the current directory:

```text
λ /users/root>
```

Clearsh's terminal is available as `/system/terminals/current`. Each terminal
process resolves that path to its own displayed screen. Writes replace the
screen; writing empty text clears it. The `(clear)` function does this, then
Clearsh prints the next prompt:

```text
λ /users/root> (clear)
λ /users/root>
```

Use Left/Right/Home/End to edit the current line and Up/Down to browse the
eight most recent commands; moving down past the newest command restores the
unfinished draft. Ctrl+Left/Right move by words, treating whitespace and Lisp
delimiters as boundaries. Ctrl+A/E move to the line start/end, Ctrl+U/K delete
to the left/right, Ctrl+W deletes the previous word, and Ctrl+L redraws the
screen. On the framebuffer console, set `/system/config/clearsh/zoom` to `1`
through `4` to scale the display; the default is `1`, and changes apply
immediately. For example:

```lisp
(write (file /system/config/clearsh/zoom) (text "2"))
```

On framebuffer systems, `/system/config/resolution` selects a 32-bit VBE
linear-framebuffer mode at runtime. For example:

```lisp
(write (file /system/config/resolution) (text "800x600x32\n"))
```

Only modes exposed by the firmware are available; an unsupported mode leaves
the current display unchanged. VGA text mode cannot change pixel resolution.
Tab completes Clearsh function names and unquoted paths inside `(file ...)`;
a unique match is inserted, while
multiple matches extend the common prefix and list candidates.
Opening parentheses and quotes insert their matching closer; unmatched closers
are completed when the line is submitted. Serial terminals may use ANSI
Ctrl+Arrow sequences (for example, `ESC [ 1 ; 5 D` for Ctrl+Left).

## Expressions

Clearsh evaluates one prefix expression per line:

```lisp
(function argument ...)
```

Values include text, numbers, booleans, paths, files, lists, functions, and
`nil`. A bare path token is relative to the current directory unless it starts
with `/`. Quoted strings support `\n`, `\t`, `\"`, and `\\` escapes.

## Files and directories

```lisp
(ls)
(ls (file /system/info))
(cd (file /system/info))
(pwd)
(print (read (file architecture)))
(write (file /tmp/note) (text "hello\n"))
```

`file` normalizes relative paths and `..`. `read` and `write` operate on files;
`ls` lists a directory, `cd` changes the current directory, and `pwd` returns
it. The filesystem includes `/system/info`, `/system/config/clearsh`,
`/system/config/pioneer`, `/system/config/resolution`, `/system/processes`,
`/users/root/example-scripts`, `/tmp`, and the root-level guides
`/README.TXT`, `/CLEARSH.TXT`, `/BRAIN.TXT`, and `/SYSTEM.TXT`. Run an example
with `(run (file /users/root/example-scripts/hello.csh))`; `write-note.csh`
demonstrates writing a file under `/tmp`. Read the guide
index with `(print (read (file /README.TXT)))`. It supports 64 entries, paths
up to 255 characters, and files up to 4096 bytes. Disk-image boots preserve
changes across reboots.

## Lists and lambdas

`map` applies a function to each list item, while `filter` keeps items whose
result is truthy. Lambdas take one parameter and one expression. Both the
Unicode `λ` symbol and the ASCII `lambda` spelling are accepted:

```lisp
(print (map (λ (item) (text item)) (list 1 "two")))
(print (filter (lambda (item) (text item)) (list "" "keep")))
```

Built-in functions such as `text` can also be passed directly to `map` and
`filter`.

## Math and control

```lisp
(+ 2 (* 3 4))
(/ 20 3)
(pow 2 8)
(if (> 5 2) "yes" "no")
(and true (not false))
(concat "Lucid " "Kernel")
(length (list 1 2 3))
(uptime)
```

Arithmetic uses signed 32-bit integers; division is integer division.
Overflow and division by zero return errors. `if` evaluates only the selected
branch. `(uptime)` returns seconds since boot.

## System calls

Clearsh offers a checked `(syscall number ...)` form for the two filesystem
calls. It accepts shell values and creates any user buffers internally, so
scripts cannot pass raw pointers:

```lisp
(syscall 3 (file /system/info/syscalls))
(syscall 4 (file /users/root/note) (text "saved"))
```

Only `SYS_FS_READ` (3) and `SYS_FS_WRITE` (4) are numbered syscalls.
Unsupported numbers or invalid argument types return an error. Process
creation, waiting, exit, timing, file watches, and POSIX compatibility are
available through virtual files.
File and console I/O—including `read`, `write`, `ls`, `vi`, and `run`—uses
syscalls. Math, logic, and list/lambda evaluation remain Clearsh intrinsics.
`ls` prints a file's name when given a file path; directory paths still list
their children.

Kernel operations are available as virtual files:

```lisp
(ls (file /system/syscalls))
(write (file /system/terminals/current) (text "hello\n"))
(print (read (file /system/terminals/current)))
(print (read (file /system/syscalls/get_ticks)))
(print (read (file /system/syscalls/terminal_size)))
(print (read (file /system/time/wait/10)))
```

`readkey` waits for input and returns a numeric key code; `reap` releases exited
children. `shutdown` is a write-only control file, and writing to it powers off
the machine. The syscall directory is virtual; its endpoints cannot be replaced
with ordinary file contents. See `SYSCALLS.md` for the full endpoint list and
per-file behavior.

The virtual `/system/processes` directory provides a process table, a service
control endpoint, and current-process lifecycle files:

```lisp
(print (read (file /system/processes/list)))
(write (file /system/processes/kill) (text "brain"))
(print (read (file /system/processes/current/cwd)))
```

`kill` accepts an image path or a unique image basename. It cannot terminate
the current process or PID 1; ambiguous or missing names return an error.

## Other functions

```lisp
(print value)
(text value)
(list value ...)
(run (file /users/root/start.csh))
(vi (file /users/root/start.csh))
(exit)
(install)
(shutdown)
(help)
```

`(help)` prints concise syntax and examples, including where to read runtime
system details and the syscall list (`/system/info/syscalls`). Syscalls are
available through read/write and the typed `(syscall number ...)` form; other
kernel operations use virtual files. Raw user pointers are never accepted.

`vi` (also named `edit`) opens a small modal editor. In normal mode, use
`i`/`a` to insert, `o`/`O` to open a line, `h`/`j`/`k`/`l` or arrows to move,
`x` to delete, `u` to undo one edit, and `:w`, `:q`, `:wq`, or `:q!` to
save/quit. The editor supports one 4096-byte buffer.

`(install)` explains how to build a bootable disk image with
`./build.sh disk`; Clearsh does not yet partition or overwrite a physical disk.
`(shutdown)` and `(poweroff)` request ACPI S5 poweroff when firmware exposes
the required tables, then try the QEMU/Bochs legacy power ports. When a QEMU
guest powers off, QEMU exits normally; this does not shut down the host.

Pioneer reads `/system/config/pioneer/boot` before showing the prompt. Each
nonblank line names a Clearsh script; use the `.csh` extension. Relative paths
resolve inside
`/system/config/pioneer/`, absolute paths are accepted, and lines beginning
with `#` are comments. Edit the list or scripts with `(vi (file ...))`.
Start each script with `#!/system/bin/clearsh`; Clearsh ignores the shebang
line when running it. This identifies the interpreter; Clearsh does not
implement shell-style variable assignments such as `CLEARSH=1`.
Scripts contain one expression per line.
Only the embedded Pioneer and Clearsh images are currently executable.
There is no general executable loader, preemptive multitasking, or POSIX
compatibility.
