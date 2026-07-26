#!/bin/sh
# build.sh - build and run myos
# Usage:
#   ./build.sh                  build only
#   ./build.sh run              build + boot in QEMU (graphical window)
#   ./build.sh clean            remove build artifacts
#   ./build.sh install-tcc      build tcc      (local, no sudo, no system deps used)
#   ./build.sh install-xorriso  build xorriso  (local, no sudo, no system deps used)
#   ./build.sh install-mtools   build mtools   (local, no sudo, no system deps used)
#   ./build.sh install-grub     build grub     (local, no sudo, no system deps used)
#   ./build.sh install-all      build all of the above, in dependency order
#
# All install-* targets build into thirdparty/local and NEVER use anything
# already on the system, even if a system copy exists -- this keeps the
# whole toolchain self-contained and reproducible. build/run put
# thirdparty/local/bin first on PATH so these are what actually get used.

set -e

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
PREFIX="$SCRIPT_DIR/thirdparty/local"

TCC_SRC_DIR="${TCC_SRC_DIR:-$SCRIPT_DIR/thirdparty/src/tinycc}"

XORRISO_VERSION="${XORRISO_VERSION:-1.5.8.pl02}"
XORRISO_URL="${XORRISO_URL:-https://www.gnu.org/software/xorriso/xorriso-${XORRISO_VERSION}.tar.gz}"

MTOOLS_VERSION="${MTOOLS_VERSION:-4.0.49}"
MTOOLS_URL="${MTOOLS_URL:-http://ftp.gnu.org/gnu/mtools/mtools-${MTOOLS_VERSION}.tar.gz}"

GRUB_VERSION="${GRUB_VERSION:-2.12}"
GRUB_URL="${GRUB_URL:-https://ftp.gnu.org/gnu/grub/grub-${GRUB_VERSION}.tar.gz}"

# Prefer anything we've built locally over system tools.
PATH="$PREFIX/bin:$PATH"

CC=tcc
AS=nasm
LD=ld

CFLAGS="-m32 -ffreestanding -nostdinc -I include -Wall -Wextra -c"
ASFLAGS="-f elf32"
LDFLAGS="-m elf_i386 -T linker.ld -nostdlib"

SRC_DIR=src
BUILD_DIR=build
ISODIR=isodir
ISO=lucid.iso
BIN=lucid.bin

clean() {
    rm -rf "$BUILD_DIR" "$BIN" "$ISO" "$ISODIR"
    echo "cleaned"
}

build() {
    mkdir -p "$BUILD_DIR"
    objs=""

    echo "[as] boot.s"
    "$AS" $ASFLAGS boot.s -o "$BUILD_DIR/boot.o"
    objs="$objs $BUILD_DIR/boot.o"

    for src in "$SRC_DIR"/*.c; do
        [ -e "$src" ] || continue
        obj="$BUILD_DIR/$(basename "${src%.c}").o"
        echo "[cc] $src"
        "$CC" $CFLAGS "$src" -o "$obj"
        objs="$objs $obj"
    done

    echo "[ld] linking $BIN"
    "$LD" $LDFLAGS -o "$BIN" $objs

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

    echo "build ok -> $ISO"
}

QEMU_DISPLAY="${QEMU_DISPLAY:-sdl}"

run() {
    build
    qemu-system-i386 -cdrom "$ISO" -display "$QEMU_DISPLAY" -vga std
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

    ensure_gitignored "thirdparty/"

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
        echo "[install-tcc] OK: thirdparty/local/bin/tcc supports -m32"
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

    ensure_gitignored "thirdparty/"

    src_dir="$(fetch_and_extract "$XORRISO_URL" "$XORRISO_VERSION" "xorriso-*" "$SCRIPT_DIR/thirdparty/src")"
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

    ensure_gitignored "thirdparty/"

    src_dir="$(fetch_and_extract "$MTOOLS_URL" "$MTOOLS_VERSION" "mtools-*" "$SCRIPT_DIR/thirdparty/src")"
    echo "[install-mtools] source at $src_dir"

    echo "[install-mtools] configuring, prefix=$PREFIX"
    ( cd "$src_dir" && ./configure --prefix="$PREFIX" )

    echo "[install-mtools] building"
    ( cd "$src_dir" && make -j"$(nproc 2>/dev/null || echo 1)" )

    echo "[install-mtools] installing locally to $PREFIX (no sudo)"
    ( cd "$src_dir" && make install )

    echo "[install-mtools] verifying"
    if [ -x "$PREFIX/bin/mformat" ]; then
        echo "[install-mtools] OK: thirdparty/local/bin/mformat installed"
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

    ensure_gitignored "thirdparty/"

    src_dir="$(fetch_and_extract "$GRUB_URL" "$GRUB_VERSION" "grub-*" "$SCRIPT_DIR/thirdparty/src")"
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
    echo "[install-all] done. thirdparty/local/bin now has a fully self-contained toolchain."
}

case "$1" in
    clean)           clean ;;
    run)             run ;;
    install-tcc)     install_tcc ;;
    install-xorriso) install_xorriso ;;
    install-mtools)  install_mtools ;;
    install-grub)    install_grub ;;
    install-all)     install_all ;;
    "")              build ;;
    *)
        echo "usage: $0 [run|clean|install-tcc|install-xorriso|install-mtools|install-grub|install-all]" >&2
        exit 1
        ;;
esac
