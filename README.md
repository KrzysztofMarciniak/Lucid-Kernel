# Lucid Kernel

A non-POSIX, KISS kernel. Everything is a file.

![Logo](LogoLucid.png)

## Philosophy

- **KISS above all** — the simplest correct implementation wins over the flexible/general one.
- **Non-POSIX** — no obligation to POSIX semantics, syscalls, or compatibility layers.
- **Everything is a file** — processes, devices, IPC, network, memory: all reachable and
  controllable through one namespace. No ioctl. No sysctl. No hidden side channels.

## Hardware Requirements (minimum)

- CPU: 2+ cores, 32-bit or above
- RAM: 8 MB

Lucid is confirmed working on the Lenovo ThinkPad X201.

## Size Budget

- Kernel image: **max 1 MB**
- ISO image: **target 10 MB**

These are hard constraints, not aspirations. If a feature doesn't fit the budget, it doesn't
ship until something else is cut or the design is simplified.

## Build Toolchain

Toolchain components can be built locally without sudo. Run
`./build.sh install-all` to bootstrap them; this requires Git, GCC, GNU Make,
tar, curl or wget, Bison, and Flex on the host. Building the ISO also requires
NASM, GNU ld, and Python 3.

- **tcc** — C99 compiler
- **python3** — build-time BDF font conversion
- **mtools** — FAT image manipulation
- **xorriso** — ISO 9660 image creation
- **grub** — bootloader (temporary; to be replaced by something simpler later)

These are host-side build tools, not programs in the booted kernel image.
Clearsh cannot run TCC yet: Lucid does not have a general executable loader or
an ISO filesystem interface. `exec` currently supports only the embedded
Brain, Eye, Pioneer, and Clearsh images. Brain starts first and launches an
Eye terminal running Clearsh.

## Usage

```
./build.sh                  build only
./build.sh run              build + boot in QEMU (serial output in terminal)
./build.sh test             build + boot headless, serial output to stdout
./build.sh disk             create a bootable 128 MiB BIOS disk image
./build.sh disk-run         create and boot that image in QEMU
./build.sh clean            remove build artifacts
./build.sh install-tcc      build TCC locally
./build.sh install-xorriso  build xorriso locally
./build.sh install-mtools   build mtools locally
./build.sh install-grub     build GRUB locally
./build.sh install-all      build all of the above, in dependency order
make get-font               download the Curie bitmap font
```

`tools/lucidc.py` is a small host-side cross-compiler for a C-like subset that
emits 32-bit Lucid user images. See [LUCIDC.md](LUCIDC.md) for the language,
compiler command, and current image-loader limitations.

### Compile and run

From the repository root, fetch the font once and build the bootable ISO:

```sh
make get-font
./build.sh
```

`./build.sh run` boots into the terminal-only serial console by default, using
text-mode dimensions instead of assuming a graphical framebuffer. Use
`QEMU_DISPLAY=gtk ./build.sh run` for the graphical framebuffer VM, or
`./build.sh test` for a headless serial smoke test. If the bundled compiler
and image tools are missing, `./build.sh install-all` builds local copies under
`.thirdparty/local`.

### Build configuration

`build.sh` reads its build settings from [`config.mk`](config.mk). The main
settings are:

| Setting | Meaning |
|---|---|
| `INIT_BOOT` | Comma-separated list of boot recipes. Order matters: the build checks that each recipe's dependencies appear earlier in the list. |
| `<RECIPE>_IMPL` | Selects the implementation directory for a recipe; for example, `TIMER_IMPL=pit250hz` selects `impl/timer/pit250hz`. The selected directory must exist. |
| `MEMORY_MB` | RAM assigned to QEMU and the physical-memory range managed by the kernel's frame allocator. The allocator assumes this range is contiguous and does not yet read the firmware memory map. |
| `LOW_RESERVED_MB` | Physical memory below this many MiB is reserved and never allocated; the default is 4 MiB. Use `1 <= LOW_RESERVED_MB < MEMORY_MB`. |
| `*_TEST_IMPL` | Test implementation selectors currently present in `config.mk`; `build.sh` does not use them in the normal build. |

For example, set `MEMORY_MB=512` to give QEMU 512 MiB and configure the
allocator for the same range. After changing `config.mk`, run `./build.sh` to
rebuild the kernel and ISO. The size-budget checks still apply.

### Use syscalls

The easiest way to try a syscall in Clearsh is through its virtual syscall
files:

```lisp
(write (file /system/syscalls/write) (text "Hello from Lucid!\n"))
(print (read (file /system/syscalls/get_ticks)))
```

Clearsh also provides a checked `(syscall number ...)` form. For example,
`(syscall 0 (text "Hello from Lucid!\n"))` writes text and `(syscall 9)` reads
the PIT tick count. Arguments are typed shell values; Clearsh does not expose
raw kernel pointers.

A ring-3 C program uses the wrapper in `recipes/syscall.h`. This example calls
`SYS_WRITE`, passing the address and byte length of a user-space string:
the wrapper invokes `int 0x80` with the syscall number in `EAX`, arguments in
`EBX`/`ECX`/`EDX`/`ESI`, and reads the result from `EAX`.

```c
#define LUCID_USER
#include "recipes/syscall.h"

__attribute__((section(".entry")))
void example_user_entry(void)
{
    static const char message[] = "Hello from ring 3!\n";
    uint32_t result = lucid_user_syscall(
        SYS_WRITE,
        (uint32_t)message,
        sizeof(message) - 1,
        0,
        0
    );

    if (result == SYS_UNKNOWN_TEST) {
        lucid_user_syscall(SYS_EXIT, 1, 0, 0, 0);
        for (;;) { }
    }
}
```

On success, `SYS_WRITE` returns the number of bytes written; failures return
`SYS_UNKNOWN_TEST`. The kernel validates user pointers before accessing them.
See [SYSCALLS.md](SYSCALLS.md) for the syscall numbers, argument registers,
return values, and instructions for adding a syscall. User images must
currently be embedded and registered in the kernel before `exec` can run them.

### Make a bootable USB

Build `lucid.iso` as above, then identify the USB device and unmount any of its
mounted partitions:

```sh
lsblk -o NAME,SIZE,MODEL,TRAN,MOUNTPOINTS
```

Unmount every mounted partition on the USB (repeat for each listed partition,
replacing the example device name as needed):

```sh
sudo umount /dev/sdX1
```

Write the ISO to the whole device (replace `/dev/sdX` with the USB device,
not a partition such as `/dev/sdX1`):

```sh
sudo dd if=lucid.iso of=/dev/sdX bs=4M status=progress conv=fsync
sync
```

> **Warning:** This erases the selected device. Verify its name and size with
> `lsblk` before running `dd`; choosing the wrong device can destroy your
> computer's data.

Safely eject the USB, connect it to the target computer, and select it from
the boot menu (F12 on the ThinkPad X201). The ISO boots in legacy BIOS mode.
It is a live image: files kept only in RAMFS are lost when the machine powers
off.

Lucid requests a 1024x768x32 GRUB framebuffer and renders console text with
Curie's 6x12 regular BDF glyphs.
If the bootloader cannot provide a compatible RGB framebuffer, output falls
back to VGA text mode. The font's SIL Open Font License is included in the
boot image. On the framebuffer console, Clearsh zoom is set by writing `1`
through `4` to `/system/config/clearsh/zoom` (default `1`); the display and
terminal dimensions update immediately. VGA text mode keeps its native size.

To compare against the standard VGA text font, build and boot with
`LUCID_TEXT_MODE=1 QEMU_DISPLAY=gtk ./build.sh run`. This skips GRUB's graphics
mode and exercises the kernel's VGA text fallback; the default framebuffer
mode is unchanged.

Clearsh supports Ctrl+Left/Right word movement, Ctrl+A/E line boundaries,
Ctrl+U/K deletion to the beginning/end of the line, Ctrl+W deletion of the
previous word, and Ctrl+L to clear the display. Tab completes Clearsh function
names and filesystem paths. Common syscalls are exposed as virtual files under
`/system/syscalls`; see [Use syscalls](#use-syscalls) for examples. Set
framebuffer zoom with
`(write (file /system/config/clearsh/zoom) (text "2"))`. The current edit
position is highlighted, and `vi` redraws only changed screen spans, avoiding
full-screen redraws on keystrokes. Boot output is preserved after the initial
banner, and the terminal halts while waiting for input instead of
busy-spinning. Built-in runtime guides are available at `/README.txt` and
under `/help`.
Poweroff uses ACPI S5 when firmware provides it, with QEMU/Bochs legacy-port
fallback; shutting down a QEMU guest exits QEMU but leaves the host running.

Brain uses a dwm-inspired master/stack layout. Mod is either Alt key:
Mod+Enter starts an Eye, Mod+Q closes the focused Eye, and Mod+Shift+Q exits
Brain to Clearsh. Mod+J/K changes focus, Mod+Shift+H/L adjusts the master
pane, Mod+0..9 switches desktops, and Mod+Shift+0..9 moves the focused Eye.
The status bar shows the active desktop, windows, focus, and lifecycle messages
such as starting or killing an Eye. Its position is read from
`/system/config/brain.conf`; set `bar=top`, `bar=bottom`, `bar=left`, or
`bar=right`. The default seeded configuration is `bar=top`; Brain rereads the
file each time it starts. For example, from Clearsh run
`(write (file /system/config/brain.conf) (text "bar=left\n"))`, then exit
Clearsh to let Pioneer restart Brain with the new position. The shell starts on
desktop 1; desktop 0 remains reserved for boot output when it has no windows.
Use Mod+0 to view the boot output and Mod+1 to return to the shell.

`disk` produces `lucid-disk.img`: a GRUB/MBR boot disk with a FAT32 boot
partition and a Lucid data partition for persistent shell files. It is safe to
test with QEMU. Writing the image to a physical disk overwrites that disk;
Clearsh's `(install)` does not perform physical-disk partitioning. Disk-image
creation additionally requires host `sfdisk` and `mkfs.fat`. The current ATA
driver does not issue cache-flush commands, so persistence is verified across
guest restarts, not sudden power loss.
