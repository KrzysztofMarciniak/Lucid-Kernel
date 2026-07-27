# PACKAGE_MANAGER.md

# Lucid Package Manager

Lucid packages follow the filesystem philosophy:

> The filesystem describes the desired state. Tools make reality match it.

The package manager is a small C program responsible for:

* discovering packages
* installing packages
* updating packages
* verifying installed files
* removing unused packages

---

# Package Layout

Package data lives under:

```text
/system/packages/
├── installed/
├── cache/
└── recipes/
    └── main/
```

---

# Package Configuration

Installed packages are declared in:

```text
/system/config/packages
```

Example:

```text
vi
gcc
git
```

This file is the source of truth.

A package listed here should exist on the system.

---

# Package Recipes

Recipes are C programs.

Location:

```text
/system/packages/recipes/main/
```

Example:

```text
/system/packages/recipes/main/vi.c
```

Recipes are executed using:

```sh
tcc -run recipe.c
```

The package manager does not contain package-specific logic.

The recipe contains the package-specific logic.

---

# Recipe Responsibilities

A package recipe defines:

* package metadata
* source location
* dependencies
* build steps
* installation steps
* file ownership

Example:

```c
#include <pkg.h>

package("vi")
version("1.0");

source("https://example.org/vi.tar.gz");

depends("ncurses");

build()
{
    run("./configure");
    run("make");
}

install()
{
    copy("vi", "/system/bin/vi");
}
```

---

# Commands

## pkg sync

Synchronize the system with the package configuration.

Example:

```sh
pkg sync
```

Process:

```text
/system/config/packages
            |
            v
       package manager
            |
            v
/system/packages/recipes/main
            |
            v
          install
```

Missing packages are installed.

---

## pkg update

Update installed packages.

Example:

```sh
pkg update
```

Process:

```text
installed package
        |
        v
new recipe version
        |
        v
rebuild/reinstall
```

---

## pkg search

Search available recipes.

Example:

```sh
pkg search editor
```

Searches:

```text
/system/packages/recipes/
```

Example result:

```text
vi
neovim
nano
```

---

# Installed Package Metadata

After installation:

```text
/system/packages/installed/vi/
├── version
├── files
├── recipe
└── metadata
```

Example:

```text
/system/packages/installed/vi/files

/system/bin/vi
/system/share/man/vi
```

The file list allows:

* removal
* verification
* conflict detection

---

# Package Removal

Removing a package means removing it from:

```text
/system/config/packages
```

then:

```sh
pkg sync
```

The package manager removes packages no longer requested.

---

# Design Goals

## Simple implementation

The package manager is written in C.

Recipes are also written in C.

No custom package language is required.

---

## Transparent execution

A recipe is just a C program.

It can be inspected:

```sh
cat /system/packages/recipes/main/vi.c
```

before execution.

---

## Small core

The package manager only knows:

* how to find recipes
* how to execute recipes
* how to track installed files

Package knowledge belongs in recipes.

---

## Lucid principle

The package manager should not hide the system.

A user should understand packages by inspecting:

```text
/system/packages/
```

and configuration:

```text
/system/config/packages
```

