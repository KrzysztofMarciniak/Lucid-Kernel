# FILES.md

# Lucid Filesystem Layout

Lucid follows one principle:

> The system should be understandable by inspecting files.

The filesystem is the primary interface between the kernel, users, programs, and configuration.

The filesystem is seeded in RAM at boot. When the kernel finds a primary ATA
disk with a Lucid partition (`0xDA`), it loads and saves two checksummed
filesystem snapshots there; otherwise the filesystem is RAM-only and writes
disappear on reboot. `./build.sh disk` creates a BIOS-bootable test disk image
with a FAT32 boot partition and the persistent Lucid partition.

The current filesystem has 64 entries, paths up to 255 characters, and files
up to 4096 bytes. It supports listing, reading, and writing existing files or
creating files inside existing directories. Directory creation and deletion
are not implemented.

The initial tree includes:

```text
/
├── system/
│   ├── info/
│   │   ├── name
│   │   ├── kernel
│   │   ├── version
│   │   ├── architecture
│   │   ├── boot
│   │   ├── features
│   │   ├── memory
│   │   ├── processes
│   │   ├── storage
│   │   ├── syscalls
│   │   └── uptime
│   ├── bin/ (virtual)
│   │   ├── pioneer
│   │   └── clearsh
│   └── config/
│       ├── defaults/
│       │   ├── shell
│       │   ├── window_manager
│       │   └── terminal
│       └── pioneer/
│           ├── boot
│           ├── welcome.clearsh
│           └── system.clearsh
├── users/
│   └── root/
│       └── welcome
└── tmp/
```

Clearsh can list, read, and write files in this tree. `/system/info/memory`,
`/system/info/processes`, `/system/info/storage`, and `/system/info/uptime` are
generated when read; `/system/info/syscalls` documents the current user-mode
syscall ABI. The virtual `/system/bin` directory lists the embedded
`/system/bin/pioneer` and `/system/bin/clearsh` executables; the image contents
are in the kernel's registry rather than RAMFS files. The broader layout below
describes intended interfaces; directories and files not listed above are not
yet implemented.

---

# /system/

`/system/` is intended to contain kernel interfaces, system metadata,
configuration, devices, and managed resources.

The system directory represents the machine.

```text
/system/
├── info/
├── config/
├── cpu/
├── memory/
├── processes/
├── files/
├── devices/
├── kernel/
└── packages/
```

---

# /system/info/

General information about the running system.

```text
/system/info/
├── name
├── kernel
├── version
├── architecture
├── build
├── boot
├── features
├── memory
├── storage
├── syscalls
└── uptime
```

Example:

```text
/system/info/name

Lucid
```

```text
/system/info/kernel

Lucid Kernel
```

```text
/system/info/version

0.1.0
```

---

# /system/config/

System configuration.

Configuration files describe the desired state of the machine.

```text
/system/config/
├── defaults/
│   ├── shell
│   ├── window_manager
│   └── terminal
├── boot
├── packages
├── services
├── users
└── devices
```

Example:

```text
/system/config/packages

vi
gcc
git
```

The package list is declarative:

> A package listed here should exist on the system.

The default application paths are stored in `/system/config/defaults/`:
`shell` names `/system/bin/clearsh`, `window_manager` names
`/system/bin/brain`, and `terminal` names `/system/bin/eye`.
Eye reads `shell`, and Brain reads `terminal` when launching applications.
Pioneer reads `window_manager` at boot.
Brain keeps the combined root and manager output in log window 1, visible on
desktop 1 only. Startup switches to desktop 2 and opens a terminal there;
configured terminal windows start at 2.

---

# /system/cpu/

CPU information and state.

```text
/system/cpu/
├── architecture
├── vendor
├── cores
├── frequency
└── features
```

Example:

```text
/system/cpu/architecture

x86
```

---

# /system/memory/

Memory manager information.

```text
/system/memory/
├── total
├── used
├── available
├── pages
└── heap
```

---

# /system/processes/

Information about running processes.

```text
/system/processes/
├── list
├── kill
├── 0/
│   ├── name
│   ├── status
│   ├── memory
│   └── files
│
└── <pid>/
    ├── name
    ├── status
    ├── memory
    └── files
```

Each process exposes metadata through files.

Example:

```text
/system/processes/1/name

init
```

---

# /system/files/

Filesystem information.

```text
/system/files/
├── mounts
├── descriptors
├── cache
└── statistics
```

---

# /system/devices/

Hardware devices.

```text
/system/devices/
├── keyboard/
├── serial/
├── storage/
├── pci/
└── display/
```

Devices expose state and configuration through files.

Example:

```text
/system/devices/keyboard/layout

us
```

---

# /system/kernel/

Kernel information.

```text
/system/kernel/
├── version
├── build
├── features
└── config
```

---

# /system/packages/

Package manager data.

```text
/system/packages/
├── installed/
├── database/
└── cache/
```

Example:

```text
/system/packages/installed/vi/
├── version
├── files
└── metadata
```

---

# /users/

User-owned data.

```text
/users/
├── root/
└── <username>/
```

Example:

```text
/users/alice/
├── bin/
├── source/
├── documents/
├── config/
└── cache/
```

---

# /users/<name>/bin/

User programs.

Example:

```text
/users/alice/bin/

editor
compiler
scripts
```

---

# /users/<name>/source/

Source code.

Example:

```text
/users/alice/source/

projects/
libraries/
experiments/
```

---

# /users/<name>/config/

User configuration.

Example:

```text
/users/alice/config/

shell
editor
environment
```

---

# Design Principles

## Files are interfaces

System state should be readable and writable through files.

Prefer:

```text
read /system/info/version
```

over:

```text
special system command
```

---

## Configuration is data

The system should be configured by modifying files.

Example:

```text
/system/config/packages
```

defines installed software.

---

## Kernel provides mechanisms

The kernel provides:

* processes
* memory
* filesystems
* hardware access
* communication

Higher-level behavior belongs in userspace.

---

## Explicit over hidden

A user should be able to understand Lucid by exploring:

```text
/system/
```

and their own environment through:

```text
/users/
```

The filesystem is the system interface.
