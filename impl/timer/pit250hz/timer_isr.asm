extern timer_tick_handler
global timer_isr

section .text

timer_isr:
    pusha
    push ds
    push es
    push fs
    push gs

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push esp
    call timer_tick_handler
    add esp, 4
    test eax, eax
    jz .restore_current
    mov esp, eax

.restore_current:
    pop gs
    pop fs
    pop es
    pop ds
    popa
    iret
