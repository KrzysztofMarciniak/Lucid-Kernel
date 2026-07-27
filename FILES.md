# FILES.md

# Lucid Filesystem Layout

Lucid follows one principle:

> The system should be understandable by inspecting files.

The filesystem is the primary interface between the kernel, users, programs, and configuration.

The root filesystem is divided into:

```text
/
├── system/
└── users/
```

---

# /system/

`/system/` contains kernel interfaces, system metadata, configuration, devices, and managed resources.

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

