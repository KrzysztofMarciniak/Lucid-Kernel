global idt_lidt
idt_lidt:
    mov eax, [esp+4]
    lidt [eax]
    ret

extern isr_common_handler

isr_common_stub:
    call isr_common_handler
    add esp, 4
    iret

%macro ISR_NOERR 1
global isr%1
isr%1:
    push dword %1
    jmp isr_common_stub
%endmacro

%assign i 0
%rep 256
    ISR_NOERR i
%assign i i+1
%endrep
