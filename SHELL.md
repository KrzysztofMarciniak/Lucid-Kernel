````markdown
# Clearsh

Clearsh is the shell for Lucid Kernel.

It is a Lisp-based shell focused on filesystem manipulation.

The shell provides a small set of primitives. System functionality is exposed through files instead of dedicated commands.

---

## Design

Clearsh is based on:

- Lisp expressions
- file-based system interfaces
- small standard library
- readable syntax

The filesystem is the system API.

---

## Syntax

Clearsh uses prefix expressions.

```lisp
(function argument ...)
````

Example:

```lisp
(print (text "Hello, World!\n"))
```

---

## Files

Files are referenced using `file`.

```lisp
(file /path/to/file)
```

Read a file:

```lisp
(read (file /system/info/cpu))
```

Write a file:

```lisp
(write (file /tmp/test) (text "data"))
```

---

## System Interface

Kernel information is exposed through the filesystem.

Example:

```
/system/
    info/
        cpu
        memory
        uptime

    processes/

    devices/

    services/
```

Accessing processes:

```lisp
(read (file /system/processes))
```

Accessing CPU information:

```lisp
(read (file /system/info/cpu))
```

No special commands are required.

---

## Values

Clearsh operates on values.

Supported types:

```
text
number
boolean
list
file
path
error
nil
```

Example:

```lisp
(text "hello")
```

---

## Built-in Functions

Clearsh keeps the built-in function set minimal.

Core functions:

```lisp
file
read
write
print
text
list
map
filter
```

---

## Example

```lisp
(print
    (read
        (file /system/info/kernel)))
```

Output:

```
Lucid Kernel 0.0.1-pre
```

---

## Scripts

Clearsh scripts are plain text files.

Example:

```lisp
#!/bin/clearsh

(print (read (file /system/info/kernel)))
```

---

## Goals

Clearsh aims to provide:

* readable shell syntax
* direct system access
* no command-specific interfaces
* structured data instead of text parsing

The shell is small.
The filesystem provides the functionality.

```
```

