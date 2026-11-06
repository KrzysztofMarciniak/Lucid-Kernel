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

### Compile and run

From the repository root, fetch the font once and build the bootable ISO:

```sh
make get-font
./build.sh
```

To try it in QEMU, run `./build.sh test` for a headless boot with serial output,
or run `QEMU_DISPLAY=gtk ./build.sh run` for a graphical VM. If the bundled
compiler and image tools are missing, `./build.sh install-all` builds local
copies under `.thirdparty/local`.

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
boot image.

To compare against the standard VGA text font, build and boot with
`LUCID_TEXT_MODE=1 QEMU_DISPLAY=gtk ./build.sh run`. This skips GRUB's graphics
mode and exercises the kernel's VGA text fallback; the default framebuffer
mode is unchanged.

Clearsh supports Ctrl+Left/Right word movement, Ctrl+A/E line boundaries,
Ctrl+U/K deletion to the beginning/end of the line, Ctrl+W deletion of the
previous word, and Ctrl+L to clear the display. Tab completes Clearsh function
names and filesystem paths. The current edit position is highlighted, and
`vi` redraws only changed screen spans, avoiding full-screen redraws on
keystrokes. Boot output is preserved after the initial banner, and the
terminal halts while waiting for input instead of busy-spinning.

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
