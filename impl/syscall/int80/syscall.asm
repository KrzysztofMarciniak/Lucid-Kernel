extern syscall_dispatch
extern usermode_kernel_esp
extern usermode_kernel_resume
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
    jnz .exit_to_kernel

    pop gs
    pop fs
    pop es
    pop ds
    popa
    iret

.exit_to_kernel:
    mov esp, [usermode_kernel_esp]
    jmp usermode_kernel_resume
