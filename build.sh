#!/bin/sh
# build.sh - build and run myos
# Usage:
#   ./build.sh                  build only
#   ./build.sh run              build + boot in QEMU (graphical window)
#   ./build.sh test             build + boot headless, serial output to stdout
#   ./build.sh clean            remove build artifacts
#   ./build.sh install-tcc      build tcc      (local, no sudo, no system deps used)
#   ./build.sh install-xorriso  build xorriso  (local, no sudo, no system deps used)
#   ./build.sh install-mtools   build mtools   (local, no sudo, no system deps used)
#   ./build.sh install-grub     build grub     (local, no sudo, no system deps used)
#   ./build.sh install-all      build all of the above, in dependency order
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

TCC_SRC_DIR="${TCC_SRC_DIR:-$SCRIPT_DIR/.thirdparty/src/tinycc}"

XORRISO_VERSION="${XORRISO_VERSION:-1.5.8.pl02}"
XORRISO_URL="${XORRISO_URL:-https://www.gnu.org/software/xorriso/xorriso-${XORRISO_VERSION}.tar.gz}"

MTOOLS_VERSION="${MTOOLS_VERSION:-4.0.49}"
MTOOLS_URL="${MTOOLS_URL:-http://ftp.gnu.org/gnu/mtools/mtools-${MTOOLS_VERSION}.tar.gz}"

GRUB_VERSION="${GRUB_VERSION:-2.12}"
GRUB_URL="${GRUB_URL:-https://ftp.gnu.org/gnu/grub/grub-${GRUB_VERSION}.tar.gz}"

# Prefer anything we've built locally over system tools.
PATH="$PREFIX/bin:$PATH"

CC=tcc
#CC=gcc
AS=nasm
LD=ld

CFLAGS="-m32 -ffreestanding -nostdinc -I . -I include -DLOW_RESERVED_MB=${LOW_RESERVED_MB}  -Wall -Wextra -mno-sse -mno-sse2 -mgeneral-regs-only -c "
ASFLAGS="-f elf32"
LDFLAGS="-m elf_i386 -T linker.ld -nostdlib"

SRC_DIR=src
RECIPE_DIR=recipes
IMPL_DIR=impl
BUILD_DIR=.build
ISODIR=.isodir
ISO=lucid.iso
BIN=lucid.bin
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
    rm -rf "$BUILD_DIR" "$BIN" "$ISO" "$ISODIR"
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
    cp "$BIN" "$ISODIR/boot/$BIN"
    cat > "$ISODIR/boot/grub/grub.cfg" << EOF
set timeout=0
set default=0

menuentry "Lucid Kernel" {
    multiboot /boot/$BIN
}
EOF
    grub-mkrescue -o "$ISO" "$ISODIR"

    check_size "iso ($ISO)" "$ISO" "$MAX_ISO_BYTES"

    echo "build ok -> $ISO"
}

QEMU_DISPLAY="${QEMU_DISPLAY:-sdl}"

run() {
    build
    qemu-system-i386 -cdrom "$ISO" -boot d -display "$QEMU_DISPLAY" -vga std -m "${MEMORY_MB}M"
}

# Headless boot with serial routed to stdout -- no window, no display
# server needed. Useful as a quick sanity check (does it boot without
# a triple fault / exception halt) since idt.c already logs exceptions
# via serial_print. Ctrl-A X to exit qemu.
test() {
    build
    echo "[test] booting headless, serial -> stdout (Ctrl-A X to quit)"
	qemu-system-i386 \
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

case "$1" in
    clean)           clean ;;
    run)             run ;;
    test)            test ;;
    install-tcc)     install_tcc ;;
    install-xorriso) install_xorriso ;;
    install-mtools)  install_mtools ;;
    install-grub)    install_grub ;;
    install-all)     install_all ;;
    "")              build ;;
    *)
        echo "usage: $0 [run|test|clean|install-tcc|install-xorriso|install-mtools|install-grub|install-all]" >&2
        exit 1
        ;;
esac
