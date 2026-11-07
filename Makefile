.DEFAULT_GOAL := all

.PHONY: all build run test disk disk-run clean get-font \
	install-tcc install-xorriso install-mtools install-grub install-all help

all: build

build:
	./build.sh

run:
	./build.sh run

test:
	./build.sh test

disk:
	./build.sh disk

disk-run:
	./build.sh disk-run

clean:
	./build.sh clean

get-font:
	./build.sh get-font

install-tcc:
	./build.sh install-tcc

install-xorriso:
	./build.sh install-xorriso

install-mtools:
	./build.sh install-mtools

install-grub:
	./build.sh install-grub

install-all:
	./build.sh install-all

help:
	@printf '%s\n' \
		'Usage: make [target]' \
		'' \
		'Targets:' \
		'  all, build   Build the bootable ISO (default)' \
		'  run          Build and boot in QEMU' \
		'  test         Run the headless smoke test' \
		'  disk         Create a bootable disk image' \
		'  disk-run     Create and boot the disk image' \
		'  clean        Remove build artifacts' \
		'  get-font     Download the Curie bitmap font' \
		'  install-*    Build a local tool (tcc, xorriso, mtools, grub, all)'
