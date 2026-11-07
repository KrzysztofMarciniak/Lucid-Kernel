BITS 32
SECTION .text

GLOBAL vbe_trampoline_start
GLOBAL vbe_trampoline_end

%define SCRATCH         0x1E000
%define MODE_INFO       0x1F000
%define TRAMPOLINE_BASE 0x30000
%define REAL_CODE_SEL   0x30
%define KERNEL_CODE_SEL 0x08

vbe_trampoline_start:
    pushfd
    cli
    pushad
    mov [SCRATCH + 28], esp
    mov eax, [esp + 40]
    mov [SCRATCH], eax
    mov eax, [esp + 44]
    mov [SCRATCH + 4], eax
    mov eax, [esp + 48]
    mov [SCRATCH + 8], eax
    sidt [SCRATCH + 12]
    mov eax, cr0
    mov [SCRATCH + 20], eax
    mov eax, cr3
    mov [SCRATCH + 24], eax

    mov eax, [SCRATCH + 20]
    and eax, 0x7FFFFFFF
    mov cr0, eax
    jmp far [TRAMPOLINE_BASE + pm16_pointer - vbe_trampoline_start]

BITS 16
pm16_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    lidt [cs:real_mode_idtr - vbe_trampoline_start]
    mov eax, cr0
    and eax, 0xFFFFFFFE
    mov cr0, eax
    jmp far [cs:real_mode_pointer - vbe_trampoline_start]

real_mode_entry:
    mov ax, 0x4000
    mov ss, ax
    mov sp, 0xFFFE
    mov ax, 0x1E00
    mov ds, ax
    mov dx, [0]
    mov bx, [4]
    mov cx, [8]
    mov ax, 0x1F00
    mov es, ax
    xor di, di
    mov ax, dx
    int 0x10
    push ax
    mov ax, 0x1E00
    mov ds, ax
    pop ax
    mov [0x20], ax
    cli
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp dword far [cs:protected_mode_pointer - vbe_trampoline_start]

BITS 32
protected_mode_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov eax, [SCRATCH + 24]
    mov cr3, eax
    mov eax, [SCRATCH + 20]
    mov cr0, eax
    lidt [SCRATCH + 12]
    mov esp, [SCRATCH + 28]
    popad
    popfd
    movzx eax, word [SCRATCH + 32]
    ret

pm16_pointer:
    dd pm16_entry - vbe_trampoline_start
    dw REAL_CODE_SEL

real_mode_pointer:
    dw real_mode_entry - vbe_trampoline_start
    dw 0x3000

protected_mode_pointer:
    dd TRAMPOLINE_BASE + protected_mode_entry - vbe_trampoline_start
    dw KERNEL_CODE_SEL

real_mode_idtr:
    dw 0x03FF
    dd 0

vbe_trampoline_end:
