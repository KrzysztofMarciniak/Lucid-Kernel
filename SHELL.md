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

VGA text mode remains at its native size. Tab completes Clearsh function names
and unquoted paths inside `(file ...)`; a unique match is inserted, while
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
`/system/config/pioneer`, `/users/root`, `/tmp`, `/README.txt`, and `/help`.
Read the guide index with
`(print (read (file /help/README.txt)))`. It supports 64 entries, paths up to
255 characters, and files up to 4096 bytes. Disk-image boots preserve changes
across reboots.

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

Clearsh offers a checked `(syscall number ...)` form. It accepts shell values
and creates any user buffers internally, so scripts cannot pass raw pointers:

```lisp
(syscall 9)
(syscall 0 (text "hello\n"))
(syscall 3 (file /system/info/syscalls))
(syscall 4 (file /users/root/note) (text "saved"))
```

Numbers and argument shapes are listed in `/system/info/syscalls`. `SYS_EXIT`
(number 1) is unavailable through this generic form; use `(exit [status])` to
exit Clearsh and let Pioneer restart it. Unsupported syscall numbers or
invalid argument types return an error.
File and console I/O—including `read`, `write`, `ls`, `vi`, and `run`—uses
syscalls. Math, logic, and list/lambda evaluation remain Clearsh intrinsics.
`ls` prints a file's name when given a file path; directory paths still list
their children.

Common kernel calls are also available as virtual files:

```lisp
(ls (file /system/syscalls))
(write (file /system/syscalls/write) (text "hello\n"))
(print (read (file /system/syscalls/get_ticks)))
(print (read (file /system/syscalls/terminal_size)))
```

`readkey` waits for input and returns a numeric key code; `reap` releases exited
children. `clear` and `shutdown` are write-only control files, and writing to
`shutdown` powers off the machine. The directory is virtual; its endpoint
files cannot be replaced with ordinary file contents. See `SYSCALLS.md` for
the full endpoint list and per-file behavior.

## Other functions

```lisp
(print value)
(text value)
(list value ...)
(run (file /users/root/start.clearsh))
(vi (file /users/root/start.clearsh))
(exit)
(install)
(shutdown)
(help)
```

`(help)` prints concise syntax and examples, including where to read runtime
system details and the syscall list (`/system/info/syscalls`). Syscalls are
available through checked built-ins and the typed `(syscall number ...)` form;
raw user pointers are never accepted.

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
nonblank line names a Clearsh script; relative paths resolve inside
`/system/config/pioneer/`, absolute paths are accepted, and lines beginning
with `#` are comments. Edit the list or scripts with `(vi (file ...))`.
Scripts contain one expression per line; a leading `#!` line is ignored.
Only the embedded Pioneer and Clearsh images are currently executable.
There is no general executable loader, preemptive multitasking, or POSIX
compatibility.
