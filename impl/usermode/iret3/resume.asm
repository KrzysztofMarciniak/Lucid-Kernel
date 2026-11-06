extern usermode_kernel_esp
global usermode_resume_frame
global usermode_kernel_resume

section .text

usermode_resume_frame:
    push ebp
    push ebx
    push esi
    push edi
    mov eax, [esp + 20]
    mov [usermode_kernel_esp], esp
    mov esp, eax
    pop gs
    pop fs
    pop es
    pop ds
    popa
    iret

usermode_kernel_resume:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret
