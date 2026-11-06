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
- **python3** — build-time BDF font conversion
- **mtools** — FAT image manipulation
- **xorriso** — ISO 9660 image creation
- **grub** — bootloader (temporary; to be replaced by something simpler later)

These are host-side build tools, not programs in the booted kernel image.
Clearsh cannot run TCC yet: Lucid does not have a general executable loader or
an ISO filesystem interface. `exec` currently supports only the embedded
Pioneer and Clearsh images.

## Usage

```
./build.sh                  build only
./build.sh run              build + boot in QEMU (serial output in terminal)
./build.sh test             build + boot headless, serial output to stdout
./build.sh disk             create a bootable 128 MiB BIOS disk image
./build.sh disk-run         create and boot that image in QEMU
./build.sh clean            remove build artifacts
./build.sh install-tcc      build tcc      (local, no sudo, no system deps used)
./build.sh install-xorriso  build xorriso  (local, no sudo, no system deps used)
./build.sh install-mtools   build mtools   (local, no sudo, no system deps used)
./build.sh install-grub     build grub     (local, no sudo, no system deps used)
./build.sh install-all      build all of the above, in dependency order
make get-font               download the Curie bitmap font
```

Run `make get-font` once before building. Lucid requests a 1024x768x32 GRUB
framebuffer and renders console text with Curie's 6x12 regular BDF glyphs.
If the bootloader cannot provide a compatible RGB framebuffer, output falls
back to VGA text mode. The font's SIL Open Font License is included in the
boot image.

To compare against the standard VGA text font, build and boot with
`LUCID_TEXT_MODE=1 QEMU_DISPLAY=gtk ./build.sh run`. This skips GRUB's graphics
mode and exercises the kernel's VGA text fallback; the default framebuffer
mode is unchanged.

Clearsh supports Ctrl+Left/Right word movement, Ctrl+A/E line boundaries,
Ctrl+U/K deletion to the beginning/end of the line, Ctrl+W deletion of the
previous word, and Ctrl+L to clear the display. Tab completes Clearsh function
names and filesystem paths. The current edit position is highlighted, and
`vi` redraws in place rather than clearing the screen on each keypress. Boot
output is preserved after the initial banner, and the terminal halts while
waiting for input instead of busy-spinning.

`disk` produces `lucid-disk.img`: a GRUB/MBR boot disk with a FAT32 boot
partition and a Lucid data partition for persistent shell files. It is safe to
test with QEMU. Writing the image to a physical disk overwrites that disk;
Clearsh's `(install)` does not perform physical-disk partitioning. Disk-image
creation additionally requires host `sfdisk` and `mkfs.fat`. The current ATA
driver does not issue cache-flush commands, so persistence is verified across
guest restarts, not sudden power loss.
