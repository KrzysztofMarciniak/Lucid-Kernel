extern syscall_dispatch
global syscall_isr

section .text

syscall_isr:
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
    call syscall_dispatch
    add esp, 4
    test eax, eax
    jnz .switch_stack

    pop gs
    pop fs
    pop es
    pop ds
    popa
    iret

.switch_stack:
    test eax, eax
    jz .no_process
    mov esp, eax
    pop gs
    pop fs
    pop es
    pop ds
    popa
    iret

.no_process:
    cli
.halt:
    hlt
    jmp .halt
