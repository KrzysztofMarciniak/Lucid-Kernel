; boot.s - Multiboot header + entry point stub
; This is the ONLY assembly file we need for booting.

MBALIGN  equ  1 << 0
MEMINFO  equ  1 << 1
VIDEO    equ  1 << 2
%ifdef LUCID_TEXT_MODE
FLAGS    equ  MBALIGN | MEMINFO
%else
FLAGS    equ  MBALIGN | MEMINFO | VIDEO
%endif
MAGIC    equ  0x1BADB002
CHECKSUM equ -(MAGIC + FLAGS)

section .multiboot
align 4
    dd MAGIC
    dd FLAGS
    dd CHECKSUM
    ; Keep the fixed a.out fields before the video request fields.
    dd 0
    dd 0
    dd 0
    dd 0
    dd 0
%ifndef LUCID_TEXT_MODE
    dd 0
    dd 1024
    dd 768
    dd 32
%endif

section .bss
align 16
stack_bottom:
    resb 16384      ; 16 KiB stack
stack_top:

section .text
global _start
extern kernel_main

_start:
    mov esp, stack_top   ; set up the stack (C code needs this)
    push ebx              ; multiboot info pointer, set by GRUB before jumping here
    push eax              ; multiboot bootloader magic
    call kernel_main      ; jump into our C kernel
    cli
.hang:
    hlt
    jmp .hang
