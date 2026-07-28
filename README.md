# Lucid Kernel

A non-POSIX, KISS kernel. Everything is a file.

## Philosophy

- **KISS above all** — the simplest correct implementation wins over the flexible/general one.
- **Non-POSIX** — no obligation to POSIX semantics, syscalls, or compatibility layers.
- **Everything is a file** — processes, devices, IPC, network, memory: all reachable and
  controllable through one namespace. No ioctl. No sysctl. No hidden side channels.

## Hardware Requirements (minimum)

- CPU: 2+ cores, 32-bit or above
- RAM: 8 MB

## Size Budget

- Kernel image: **max 1 MB**
- ISO image: **target 10 MB**

These are hard constraints, not aspirations. If a feature doesn't fit the budget, it doesn't
ship until something else is cut or the design is simplified.

## Build Toolchain

Built entirely local, no sudo, no system package dependencies:

- **tcc** — C99 compiler
- **mtools** — FAT image manipulation
- **xorriso** — ISO 9660 image creation
- **grub** — bootloader (temporary; to be replaced by something simpler later)

## Usage

```
./build.sh                  build only
./build.sh run              build + boot in QEMU (graphical window)
./build.sh test             build + boot headless, serial output to stdout
./build.sh clean            remove build artifacts
./build.sh install-tcc      build tcc      (local, no sudo, no system deps used)
./build.sh install-xorriso  build xorriso  (local, no sudo, no system deps used)
./build.sh install-mtools   build mtools   (local, no sudo, no system deps used)
./build.sh install-grub     build grub     (local, no sudo, no system deps used)
./build.sh install-all      build all of the above, in dependency order
```
