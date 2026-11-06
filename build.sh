#!/bin/sh
# build.sh - build and run myos
# Usage:
#   ./build.sh                  build only
#   ./build.sh run              build + boot in QEMU (serial output in terminal)
#   ./build.sh test             build + boot headless, serial output to stdout
#   ./build.sh disk             create a bootable 128 MiB BIOS disk image
#   ./build.sh disk-run         create and boot the disk image in QEMU
#   ./build.sh clean            remove build artifacts
#   ./build.sh install-tcc      build tcc      (local, no sudo, no system deps used)
#   ./build.sh install-xorriso  build xorriso  (local, no sudo, no system deps used)
#   ./build.sh install-mtools   build mtools   (local, no sudo, no system deps used)
#   ./build.sh install-grub     build grub     (local, no sudo, no system deps used)
#   ./build.sh install-all      build all of the above, in dependency order
#   make get-font              download the Curie bitmap font
#   LUCID_TEXT_MODE=1          boot through VGA text mode for font diagnostics
#
# All install-* targets build into .thirdparty/local and NEVER use anything
# already on the system, even if a system copy exists -- this keeps the
# whole toolchain self-contained and reproducible. build/run put
# .thirdparty/local/bin first on PATH so these are what actually get used.

set -e

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
PREFIX="$SCRIPT_DIR/.thirdparty/local"

if [ -f "$SCRIPT_DIR/config.mk" ]; then
    . "$SCRIPT_DIR/config.mk"
fi
MEMORY_MB="${MEMORY_MB:-256}"
LOW_RESERVED_MB="${LOW_RESERVED_MB:-4}"
LUCID_TEXT_MODE="${LUCID_TEXT_MODE:-0}"

case "$LUCID_TEXT_MODE" in
    0|1) ;;
    *)
        echo "ERROR: LUCID_TEXT_MODE must be 0 or 1" >&2
        exit 1
        ;;
esac

TCC_SRC_DIR="${TCC_SRC_DIR:-$SCRIPT_DIR/.thirdparty/src/tinycc}"

XORRISO_VERSION="${XORRISO_VERSION:-1.5.8.pl02}"
XORRISO_URL="${XORRISO_URL:-https://www.gnu.org/software/xorriso/xorriso-${XORRISO_VERSION}.tar.gz}"

MTOOLS_VERSION="${MTOOLS_VERSION:-4.0.49}"
MTOOLS_URL="${MTOOLS_URL:-http://ftp.gnu.org/gnu/mtools/mtools-${MTOOLS_VERSION}.tar.gz}"

GRUB_VERSION="${GRUB_VERSION:-2.12}"
GRUB_URL="${GRUB_URL:-https://ftp.gnu.org/gnu/grub/grub-${GRUB_VERSION}.tar.gz}"

# Prefer anything we've built locally over system tools.
PATH="$PREFIX/bin:$PATH"

CC="${CC:-tcc}"
if [ "$CC" = tcc ]; then
    if [ -x "$PREFIX/bin/tcc" ]; then
        CC="$PREFIX/bin/tcc"
    elif [ -x "$TCC_SRC_DIR/tcc" ] && [ -x "$TCC_SRC_DIR/i386-tcc" ]; then
        CC="$TCC_SRC_DIR/tcc"
    fi
fi
#CC=gcc
AS=nasm
LD=ld

CFLAGS="-m32 -ffreestanding -nostdinc -I . -I include -DLOW_RESERVED_MB=${LOW_RESERVED_MB}  -Wall -Wextra -mno-sse -mno-sse2 -mgeneral-regs-only -c "
ASFLAGS="-f elf32"
if [ "$LUCID_TEXT_MODE" = "1" ]; then
    ASFLAGS="$ASFLAGS -DLUCID_TEXT_MODE"
fi
LDFLAGS="-m elf_i386 -T linker.ld -nostdlib"

SRC_DIR=src
RECIPE_DIR=recipes
IMPL_DIR=impl
BUILD_DIR=.build
ISODIR=.isodir
ISO=lucid.iso
BIN=lucid.bin
DISK_IMAGE="$SCRIPT_DIR/lucid-disk.img"
FONT_DIR="$SCRIPT_DIR/.thirdparty/font/curie"
INIT_TABLE_C="$BUILD_DIR/init_table.c"

# mangled_obj_path <src-file> -> unique .o path under BUILD_DIR
# Slashes become underscores so impl/keyboard/bike/bike.c and
# impl/keyboard/suv/suv.c never collide even if basenames match.
mangled_obj_path() {
    echo "$BUILD_DIR/$(echo "$1" | sed -e 's/\.c$//' -e 's/[\/]/_/g').o"
}

# Size budget (bytes). Hard constraints -- see README.md.
MAX_KERNEL_BYTES=$((1 * 1024 * 1024))
MAX_ISO_BYTES=$((10 * 1024 * 1024))

file_size() {
    # portable stat -- BSD stat and GNU stat use different flags
    stat -f%z "$1" 2>/dev/null || stat -c%s "$1" 2>/dev/null
}

human_bytes() {
    awk -v n="$1" 'BEGIN {
        if (n >= 1048576) printf "%.2f MiB", n / 1048576
        else if (n >= 1024) printf "%.2f KiB", n / 1024
        else printf "%d B", n
    }'
}

# check_size <label> <path> <max-bytes>
check_size() {
    label="$1"; path="$2"; max="$3"
    size="$(file_size "$path")"
    if [ -z "$size" ]; then
        echo "ERROR: could not determine size of $path" >&2
        exit 1
    fi

    if [ "$size" -gt "$max" ]; then
        echo "ERROR: $label is $(human_bytes "$size") -- exceeds budget of $(human_bytes "$max")" >&2
        exit 1
    fi

    echo "[size] $label: $(human_bytes "$size") / $(human_bytes "$max") budget"
}

clean() {
    rm -rf "$BUILD_DIR" "$BIN" "$ISO" "$DISK_IMAGE" "$ISODIR"
    echo "cleaned"
}

# resolve_impl <recipe-name> -> prints the chosen impl directory name
# Looks up <RECIPE>_IMPL from config.mk (e.g. keyboard -> KEYBOARD_IMPL).
resolve_impl() {
    recipe="$1"
    var_name="$(echo "$recipe" | tr '[:lower:]' '[:upper:]')_IMPL"
    impl="$(eval "printf '%s' \"\${$var_name:-}\"")"

    if [ -z "$impl" ]; then
        echo "ERROR: no $var_name set in config.mk for recipe '$recipe' (listed in INIT_BOOT)" >&2
        exit 1
    fi

    dir="$IMPL_DIR/$recipe/$impl"
    if [ ! -d "$dir" ]; then
        echo "ERROR: $var_name=$impl but $dir does not exist" >&2
        exit 1
    fi

    echo "$dir"
}

# check_requires <ordered-space-separated-names>
# Reads "/* REQUIRES: a, b */" from each recipe's header and verifies
# every dependency appears earlier in the list. Fails the build with
# a clear message if not.
check_requires() {
    names="$1"
    set -- $names
    i=0
    for name in $names; do
        i=$((i + 1))
        header="$RECIPE_DIR/${name}.h"
        [ -f "$header" ] || continue

        raw_deps="$(grep -oE 'REQUIRES:.*' "$header" | sed 's/REQUIRES: *//' | sed 's/\*\///' | head -1)"
        [ -z "$raw_deps" ] && continue

        old_ifs="$IFS"
        IFS=','
        for dep in $raw_deps; do
            IFS="$old_ifs"
            dep="$(echo "$dep" | xargs)"
            [ -z "$dep" ] && continue

            dep_idx=0
            this_idx=0
            j=0
            for n in $names; do
                j=$((j + 1))
                [ "$n" = "$dep" ] && dep_idx=$j
                [ "$n" = "$name" ] && [ "$this_idx" -eq 0 ] && this_idx=$j
            done

            if [ "$dep_idx" -eq 0 ]; then
                echo "ERROR: recipe '$name' requires '$dep', but '$dep' is not in INIT_BOOT at all" >&2
                exit 1
            fi
            if [ "$dep_idx" -ge "$this_idx" ]; then
                echo "ERROR: recipe '$name' requires '$dep' to appear earlier in INIT_BOOT (currently: $names)" >&2
                exit 1
            fi
            IFS=','
        done
        IFS="$old_ifs"
    done
}

# generate_init_table -> writes .build/init_table.c from INIT_BOOT,
# and echoes the list of impl .c files that need compiling (one per line).
generate_init_table() {
    if [ -z "${INIT_BOOT:-}" ]; then
        echo "ERROR: INIT_BOOT is not set in config.mk" >&2
        exit 1
    fi

    mkdir -p "$BUILD_DIR"

    names="$(echo "$INIT_BOOT" | tr ',' ' ')"

    check_requires "$names"

    {
        echo "/* AUTO-GENERATED by build.sh from INIT_BOOT in config.mk. Do not edit. */"
        echo "#include \"include/init_table.h\""
        for name in $names; do
            echo "#include \"recipes/${name}.h\""
        done
        echo ""
        echo "lucid_init_entry init_table[] = {"
        for name in $names; do
            echo "    { \"$name\", ${name}_pre, ${name}_init, ${name}_post },"
        done
        echo "};"
        echo ""
        echo "const unsigned int init_table_count = sizeof(init_table) / sizeof(init_table[0]);"
    } > "$INIT_TABLE_C"

    echo "[gen] $INIT_TABLE_C  (INIT_BOOT=$INIT_BOOT)" >&2


for name in $names; do
        impl_dir="$(resolve_impl "$name")"
        echo "[recipe] $name -> $impl_dir" >&2
        find "$impl_dir" \( -name '*.c' -o -name '*.asm' -o -name '*.s' \) -type f
    done
}

build() {
    mkdir -p "$BUILD_DIR"
    objs=""

    font_bdf="$FONT_DIR/regular/curieMedium-12.bdf"
    if [ ! -f "$font_bdf" ] || [ ! -f "$FONT_DIR/LICENSE" ]; then
        echo "ERROR: Curie font is missing; run 'make get-font' first" >&2
        exit 1
    fi
    command -v python3 >/dev/null 2>&1 || {
        echo "ERROR: python3 is required to convert the Curie BDF font" >&2
        exit 1
    }
    echo "[font] generating Curie bitmap table"
    python3 "$SCRIPT_DIR/tools/bdf_to_c.py" \
        "$font_bdf" "$BUILD_DIR/curie_font.c"
    "$CC" $CFLAGS "$BUILD_DIR/curie_font.c" -o "$BUILD_DIR/curie_font.o"
    objs="$objs $BUILD_DIR/curie_font.o"

    if echo ",${INIT_BOOT:-}," | grep -q ",usermode,"; then
        for app in pioneer clearsh; do
            app_dir="$SCRIPT_DIR/userland/$app"
            app_objs=""
            for src in "$app_dir"/*.c; do
                obj="$BUILD_DIR/${app}_user_$(basename "$src" .c).o"
                echo "[cc] $src (user image)"
                "$CC" $CFLAGS -DLUCID_USER "$src" -o "$obj"
                app_objs="$app_objs $obj"
            done
            "$CC" $CFLAGS "$SRC_DIR/string.c" \
                -o "$BUILD_DIR/${app}_string.o"
            "$LD" -m elf_i386 -T "$app_dir/user.ld" --oformat binary \
                -o "$BUILD_DIR/$app.bin" \
                $app_objs "$BUILD_DIR/${app}_string.o"
            app_size="$(file_size "$BUILD_DIR/$app.bin")"
            if [ "$app_size" -gt 65536 ]; then
                echo "ERROR: $app user image exceeds 64 KiB" >&2
                exit 1
            fi
            echo "[size] $app user image: $(human_bytes "$app_size") / 64.00 KiB"
            echo "[as] $app_dir/${app}_image.asm (embedded user image)"
            "$AS" $ASFLAGS "$app_dir/${app}_image.asm" \
                -o "$BUILD_DIR/${app}_image.o"
            objs="$objs $BUILD_DIR/${app}_image.o"
        done
    fi

    echo "[as] boot.s"
    "$AS" $ASFLAGS boot.s -o "$BUILD_DIR/boot.o"
    objs="$objs $BUILD_DIR/boot.o"

for src in "$SRC_DIR"/*.s; do
        [ -e "$src" ] || continue
        obj="$(mangled_obj_path "$src")"
        echo "[as] $src"
        "$AS" $ASFLAGS "$src" -o "$obj"
        objs="$objs $obj"
    done

    for src in "$SRC_DIR"/*.c; do
        [ -e "$src" ] || continue
        obj="$(mangled_obj_path "$src")"
        echo "[cc] $src"
        "$CC" $CFLAGS "$src" -o "$obj"
        objs="$objs $obj"
    done

    echo "[recipes] resolving INIT_BOOT -> impl sources"
    impl_srcs="$(generate_init_table)"

    IFS='
'
    for src in $impl_srcs; do
        unset IFS
        [ -e "$src" ] || continue
        case "$src" in
            *.asm|*.s)
                obj="$BUILD_DIR/$(echo "$src" | sed -e 's/\./_/g' -e 's/[\/]/_/g').o"
                echo "[as] $src"
                "$AS" $ASFLAGS "$src" -o "$obj"
                ;;
            *)
                obj="$(mangled_obj_path "$src")"
                echo "[cc] $src"
                "$CC" $CFLAGS "$src" -o "$obj"
                ;;
        esac
        objs="$objs $obj"
        IFS='
'
    done
    unset IFS

    echo "[cc] $INIT_TABLE_C"
    table_obj="$(mangled_obj_path "$INIT_TABLE_C")"
    "$CC" $CFLAGS "$INIT_TABLE_C" -o "$table_obj"
    objs="$objs $table_obj"

    echo "[ld] linking $BIN"
    "$LD" $LDFLAGS -o "$BIN" $objs

    check_size "kernel ($BIN)" "$BIN" "$MAX_KERNEL_BYTES"

    echo "[check] multiboot compliance"
    grub-file --is-x86-multiboot "$BIN" || {
        echo "ERROR: not a valid multiboot kernel" >&2
        exit 1
    }

    echo "[iso] packaging $ISO"
    mkdir -p "$ISODIR/boot/grub"
    grub_modules_dir="$PREFIX/lib/grub"
    if [ ! -f "$grub_modules_dir/i386-pc/vbe.mod" ]; then
        grub_modules_dir=/usr/lib/grub
    fi
    if [ ! -f "$grub_modules_dir/i386-pc/vbe.mod" ]; then
        echo "ERROR: GRUB BIOS VBE module was not found" >&2
        exit 1
    fi
    mkdir -p "$ISODIR/boot/grub/i386-pc"
    cp "$grub_modules_dir/i386-pc/vbe.mod" \
        "$ISODIR/boot/grub/i386-pc/vbe.mod"
    cp "$BIN" "$ISODIR/boot/$BIN"
    cat > "$ISODIR/boot/grub/grub.cfg" << EOF
set timeout=0
set default=0
terminal_output console

menuentry "Lucid Kernel" {
    if [ "$LUCID_TEXT_MODE" != "1" ]; then
        insmod all_video
        if [ "$grub_platform" = "pc" ]; then
            insmod vbe
        fi
        set gfxmode=1024x768x32
        set gfxpayload=1024x768x32
        insmod gfxterm
        terminal_output gfxterm
    else
        set gfxpayload=text
    fi
    multiboot /boot/$BIN
}
EOF
    cp "$FONT_DIR/LICENSE" "$ISODIR/boot/curie-font-LICENSE.txt"
    # Keep the hybrid image small while including GRUB's menu loader and the
    # commands needed to read the config and launch a Multiboot kernel.
    grub-mkrescue \
        --install-modules="normal configfile multiboot terminal video all_video gfxterm test" \
        --modules="normal configfile multiboot terminal video all_video gfxterm test" \
        --core-compress=xz -o "$ISO" "$ISODIR"

    check_size "iso ($ISO)" "$ISO" "$MAX_ISO_BYTES"

    echo "build ok -> $ISO"
}

build_disk_image() {
    build
    for tool in sfdisk mkfs.fat mmd mcopy grub-mkimage dd; do
        command -v "$tool" >/dev/null 2>&1 || {
            echo "ERROR: disk image creation requires '$tool'" >&2
            exit 1
        }
    done

    grub_bios_setup="$PREFIX/sbin/grub-bios-setup"
    if [ ! -x "$grub_bios_setup" ]; then
        grub_bios_setup="$(command -v grub-bios-setup || true)"
    fi
    if [ -z "$grub_bios_setup" ] || [ ! -x "$grub_bios_setup" ]; then
        echo "ERROR: disk image creation requires grub-bios-setup" >&2
        exit 1
    fi

    grub_platform_dir="$PREFIX/lib/grub/i386-pc"
    if [ ! -f "$grub_platform_dir/boot.img" ]; then
        grub_platform_dir=/usr/lib/grub/i386-pc
    fi
    if [ ! -f "$grub_platform_dir/boot.img" ]; then
        echo "ERROR: GRUB i386-pc BIOS modules were not found" >&2
        exit 1
    fi

    disk_size_mb="${LUCID_DISK_SIZE_MB:-128}"
    case "$disk_size_mb" in
        ''|*[!0-9]*)
            echo "ERROR: LUCID_DISK_SIZE_MB must be a positive integer" >&2
            exit 1
            ;;
    esac
    total_sectors=$((disk_size_mb * 2048))
    data_start=133120
    if [ "$total_sectors" -le "$data_start" ]; then
        echo "ERROR: disk image must be larger than 65 MiB" >&2
        exit 1
    fi

    stage="$BUILD_DIR/disk-stage"
    grub_stage="$stage/grub"
    mkdir -p "$stage/boot/grub" "$grub_stage"
    cp "$BIN" "$stage/boot/lucid.bin"
    cat > "$stage/boot/grub/grub.cfg" << EOF
set timeout=0
set default=0
set root=(hd0,msdos1)
terminal_input console
terminal_output console

menuentry "Lucid Kernel" {
    if [ "$LUCID_TEXT_MODE" != "1" ]; then
        insmod all_video
        if [ "$grub_platform" = "pc" ]; then
            insmod vbe
        fi
        set gfxmode=1024x768x32
        set gfxpayload=1024x768x32
        insmod gfxterm
        terminal_output gfxterm
    else
        set gfxpayload=text
    fi
    multiboot /boot/lucid.bin
}
EOF

    truncate -s 0 "$DISK_IMAGE"
    truncate -s "${disk_size_mb}M" "$DISK_IMAGE"
    printf 'label: dos\nunit: sectors\n\nstart=2048, size=131072, type=0c, bootable\nstart=%s, size=%s, type=da\n' \
        "$data_start" "$((total_sectors - data_start))" |
        sfdisk "$DISK_IMAGE" >/dev/null
    fat_image="$stage/boot-fat.img"
    rm -f "$fat_image"
    mkfs.fat -C -F 32 -h 2048 -n LUCID "$fat_image" 65536 >/dev/null
    dd if="$fat_image" of="$DISK_IMAGE" bs=512 seek=2048 \
        conv=notrunc status=none

    fat_offset=$((2048 * 512))
    mmd -i "$DISK_IMAGE@@$fat_offset" ::/boot
    mmd -i "$DISK_IMAGE@@$fat_offset" ::/boot/grub
    mcopy -i "$DISK_IMAGE@@$fat_offset" \
        "$stage/boot/lucid.bin" ::/boot/lucid.bin
    mcopy -i "$DISK_IMAGE@@$fat_offset" \
        "$stage/boot/grub/grub.cfg" ::/boot/grub/grub.cfg
    mcopy -i "$DISK_IMAGE@@$fat_offset" \
        "$FONT_DIR/LICENSE" ::/boot/curie-font-LICENSE.txt

    grub-mkimage \
        --directory="$grub_platform_dir" \
        --format=i386-pc \
        --prefix='(hd0,msdos1)/boot/grub' \
        --output="$grub_stage/core.img" \
        biosdisk part_msdos fat normal configfile multiboot terminal video all_video vbe gfxterm vga_text at_keyboard
    cp "$grub_platform_dir/boot.img" "$grub_stage/boot.img"
    printf '(hd0) %s\n' "$DISK_IMAGE" > "$grub_stage/device.map"
    "$grub_bios_setup" \
        --directory="$grub_stage" \
        --boot-image=boot.img \
        --core-image=core.img \
        --device-map="$grub_stage/device.map" \
        --force "$DISK_IMAGE"

    echo "disk image ready -> $DISK_IMAGE"
    echo "BIOS MBR: GRUB; partition 1: boot files; partition 2: persistent LucidFS"
}

QEMU_DISPLAY="${QEMU_DISPLAY:-none}"

run_qemu() {
    if command -v qemu-system-i386 >/dev/null 2>&1; then
        qemu-system-i386 "$@"
    elif command -v qemu-system-x86_64 >/dev/null 2>&1; then
        qemu-system-x86_64 "$@"
    else
        echo "ERROR: QEMU not found (on Alpine, install with: apk add qemu-system-x86_64)" >&2
        exit 1
    fi
}

run() {
    build
    run_qemu -cdrom "$ISO" -boot d -display "$QEMU_DISPLAY" -serial stdio -m "${MEMORY_MB}M"
}

# Headless boot with serial routed to stdout -- no window, no display
# server needed. Useful as a quick sanity check (does it boot without
# a triple fault / exception halt) since idt.c already logs exceptions
# via serial_print. Ctrl-A X to exit qemu.
test() {
    build
    echo "[test] booting headless, serial -> stdout (Ctrl-A X to quit)"
	run_qemu \
    -cdrom "$ISO" \
    -boot d \
    -m "${MEMORY_MB}M" \
    -display none \
    -serial stdio \
    -no-reboot \
    -no-shutdown \
    -d int,cpu_reset,guest_errors \
    -D qemu.log
}

disk() {
    build_disk_image
}

disk_run() {
    build_disk_image
    run_qemu \
        -drive "file=$DISK_IMAGE,format=raw,if=ide" \
        -boot c \
        -m "${MEMORY_MB}M" \
        -display "$QEMU_DISPLAY" \
        -serial stdio \
        -monitor none \
        -no-reboot
}

ensure_gitignored() {
    pattern="$1"
    gi="$SCRIPT_DIR/.gitignore"

    if [ ! -f "$gi" ]; then
        echo "[thirdparty] no .gitignore found, creating one with $pattern"
        echo "$pattern" > "$gi"
        return
    fi

    if grep -qxF "$pattern" "$gi" || grep -qxF "${pattern%/}" "$gi"; then
        :
    else
        echo "[thirdparty] adding $pattern to .gitignore"
        printf '\n%s\n' "$pattern" >> "$gi"
    fi
}

need_fetcher() {
    if command -v curl >/dev/null 2>&1; then
        echo "curl -fL -o"
    elif command -v wget >/dev/null 2>&1; then
        echo "wget -O"
    else
        echo "ERROR: need curl or wget to download source" >&2
        exit 1
    fi
}

# fetch_and_extract <url> <version> <name-prefix-glob> <src-root>
# Downloads+extracts if no matching dir already exists under src-root.
# Prints the resulting source directory path on stdout.
fetch_and_extract() {
    url="$1"; version="$2"; glob="$3"; src_root="$4"
    mkdir -p "$src_root"

    existing="$(find "$src_root" -maxdepth 1 -type d -name "$glob" 2>/dev/null | head -1)"
    if [ -n "$existing" ]; then
        echo "$existing"
        return
    fi

    fetch="$(need_fetcher)"
    tarball="$src_root/$(basename "$url")"
    $fetch "$tarball" "$url" >&2

    extracted_dir="$(tar -tzf "$tarball" | head -1 | cut -d/ -f1)"
    tar -xzf "$tarball" -C "$src_root"
    rm -f "$tarball"

    echo "$src_root/$extracted_dir"
}

install_tcc() {
    if [ -x "$PREFIX/bin/tcc" ] && "$PREFIX/bin/tcc" -m32 -v >/dev/null 2>&1; then
        echo "[install-tcc] already built locally, nothing to do"
        return
    fi

    command -v git >/dev/null 2>&1 || {
        echo "ERROR: git is required to fetch tcc source" >&2
        exit 1
    }
    command -v gcc >/dev/null 2>&1 || {
        echo "ERROR: gcc is required to bootstrap-build tcc (one-time only)" >&2
        exit 1
    }

    ensure_gitignored ".thirdparty/"

    if [ -d "$TCC_SRC_DIR" ]; then
        echo "[install-tcc] found existing source at $TCC_SRC_DIR, pulling latest"
        ( cd "$TCC_SRC_DIR" && git pull )
    else
        echo "[install-tcc] cloning tinycc into $TCC_SRC_DIR"
        mkdir -p "$(dirname "$TCC_SRC_DIR")"
        git clone https://repo.or.cz/tinycc.git "$TCC_SRC_DIR"
    fi

    echo "[install-tcc] configuring with cross-target support, prefix=$PREFIX"
    ( cd "$TCC_SRC_DIR" && ./configure --enable-cross --prefix="$PREFIX" )

    echo "[install-tcc] building"
    ( cd "$TCC_SRC_DIR" && make -j"$(nproc 2>/dev/null || echo 1)" )

    echo "[install-tcc] installing locally to $PREFIX (no sudo)"
    ( cd "$TCC_SRC_DIR" && make install )

    echo "[install-tcc] verifying -m32 support"
    if "$PREFIX/bin/tcc" -m32 -v >/dev/null 2>&1; then
        echo "[install-tcc] OK: .thirdparty/local/bin/tcc supports -m32"
    else
        echo "[install-tcc] WARNING: tcc -m32 still failing, check output above" >&2
        exit 1
    fi
}

install_xorriso() {
    if [ -x "$PREFIX/bin/xorriso" ]; then
        echo "[install-xorriso] already built locally, nothing to do"
        return
    fi

    command -v tar >/dev/null 2>&1 || {
        echo "ERROR: tar is required to unpack xorriso source" >&2
        exit 1
    }

    ensure_gitignored ".thirdparty/"

    src_dir="$(fetch_and_extract "$XORRISO_URL" "$XORRISO_VERSION" "xorriso-*" "$SCRIPT_DIR/.thirdparty/src")"
    echo "[install-xorriso] source at $src_dir"

    echo "[install-xorriso] configuring, prefix=$PREFIX"
    ( cd "$src_dir" && ./configure --prefix="$PREFIX" )

    echo "[install-xorriso] building"
    ( cd "$src_dir" && make -j"$(nproc 2>/dev/null || echo 1)" )

    echo "[install-xorriso] installing locally to $PREFIX (no sudo)"
    ( cd "$src_dir" && make install )

    echo "[install-xorriso] verifying"
    if [ -x "$PREFIX/bin/xorriso" ]; then
        echo "[install-xorriso] OK: $("$PREFIX/bin/xorriso" -version 2>&1 | head -1)"
    else
        echo "[install-xorriso] WARNING: xorriso still not found after install" >&2
        exit 1
    fi
}

install_mtools() {
    if [ -x "$PREFIX/bin/mformat" ]; then
        echo "[install-mtools] already built locally, nothing to do"
        return
    fi

    command -v tar >/dev/null 2>&1 || {
        echo "ERROR: tar is required to unpack mtools source" >&2
        exit 1
    }

    ensure_gitignored ".thirdparty/"

    src_dir="$(fetch_and_extract "$MTOOLS_URL" "$MTOOLS_VERSION" "mtools-*" "$SCRIPT_DIR/.thirdparty/src")"
    echo "[install-mtools] source at $src_dir"

    echo "[install-mtools] configuring, prefix=$PREFIX"
    ( cd "$src_dir" && ./configure --prefix="$PREFIX" )

    echo "[install-mtools] building"
    ( cd "$src_dir" && make -j"$(nproc 2>/dev/null || echo 1)" )

    echo "[install-mtools] installing locally to $PREFIX (no sudo)"
    ( cd "$src_dir" && make install )

    echo "[install-mtools] verifying"
    if [ -x "$PREFIX/bin/mformat" ]; then
        echo "[install-mtools] OK: .thirdparty/local/bin/mformat installed"
    else
        echo "[install-mtools] WARNING: mformat still not found after install" >&2
        exit 1
    fi
}

install_grub() {
    if [ -x "$PREFIX/bin/grub-mkrescue" ]; then
        echo "[install-grub] already built locally, nothing to do"
        return
    fi

    command -v tar >/dev/null 2>&1 || {
        echo "ERROR: tar is required to unpack grub source" >&2
        exit 1
    }
    command -v bison >/dev/null 2>&1 || {
        echo "ERROR: bison is required to build grub (build-time only, not linked into output)" >&2
        exit 1
    }
    command -v flex >/dev/null 2>&1 || {
        echo "ERROR: flex is required to build grub (build-time only)" >&2
        exit 1
    }
    command -v python3 >/dev/null 2>&1 || {
        echo "ERROR: python3 is required to build grub (build-time only)" >&2
        exit 1
    }

    ensure_gitignored ".thirdparty/"

    src_dir="$(fetch_and_extract "$GRUB_URL" "$GRUB_VERSION" "grub-*" "$SCRIPT_DIR/.thirdparty/src")"
    echo "[install-grub] source at $src_dir"

    echo "[install-grub] configuring for BIOS (i386-pc) platform, prefix=$PREFIX"
    ( cd "$src_dir" && ./configure --prefix="$PREFIX" --target=i386 --with-platform=pc --disable-werror )

    # Known grub build issue: extra_deps.lst is a generated file, and
    # parallel make can race past the rule that creates it. Touch it
    # first and build single-threaded to avoid the race entirely.
    mkdir -p "$src_dir/grub-core"
    touch "$src_dir/grub-core/extra_deps.lst"

    echo "[install-grub] building (single-threaded, this one takes a while)"
    ( cd "$src_dir" && make )

    echo "[install-grub] installing locally to $PREFIX (no sudo)"
    ( cd "$src_dir" && make install )

    echo "[install-grub] verifying"
    if [ -x "$PREFIX/bin/grub-mkrescue" ] && [ -d "$PREFIX/lib/grub/i386-pc" ]; then
        echo "[install-grub] OK: grub-mkrescue + i386-pc BIOS modules installed locally"
    else
        echo "[install-grub] WARNING: grub-mkrescue or i386-pc modules missing after install" >&2
        exit 1
    fi
}

install_all() {
    install_tcc
    install_xorriso
    install_mtools
    install_grub
    echo "[install-all] done. .thirdparty/local/bin now has a fully self-contained toolchain."
}

get_font() {
    font_bdf="$FONT_DIR/regular/curieMedium-12.bdf"
    if [ -f "$font_bdf" ] && [ -f "$FONT_DIR/LICENSE" ]; then
        echo "[get-font] Curie font already available at $FONT_DIR"
        return
    fi

    if [ -e "$FONT_DIR" ]; then
        echo "ERROR: incomplete font directory at $FONT_DIR; resolve it before retrying" >&2
        exit 1
    fi
    command -v git >/dev/null 2>&1 || {
        echo "ERROR: git is required to download the Curie font" >&2
        exit 1
    }
    mkdir -p "$(dirname "$FONT_DIR")"
    echo "[get-font] downloading Curie from oppiliappan/curie"
    git clone --depth 1 https://github.com/oppiliappan/curie.git "$FONT_DIR"
    if [ ! -f "$font_bdf" ] || [ ! -f "$FONT_DIR/LICENSE" ]; then
        echo "ERROR: downloaded Curie repository is missing the expected BDF font or license" >&2
        exit 1
    fi
    echo "[get-font] installed Curie regular 12px BDF and OFL license"
}

case "$1" in
    clean)           clean ;;
    run)             run ;;
    test)            test ;;
    disk)            disk ;;
    disk-run)        disk_run ;;
    install-tcc)     install_tcc ;;
    install-xorriso) install_xorriso ;;
    install-mtools)  install_mtools ;;
    install-grub)    install_grub ;;
    install-all)     install_all ;;
    get-font)        get_font ;;
    "")              build ;;
    *)
        echo "usage: $0 [run|test|disk|disk-run|clean|install-tcc|install-xorriso|install-mtools|install-grub|install-all|get-font]" >&2
        exit 1
        ;;
esac
