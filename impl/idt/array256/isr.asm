; isr.s -- 256 raw ISR entry stubs + common trampoline.
; CPU error-code exceptions are handled separately.

extern isr_common_handler
global isr_stub_table

section .text

%macro ISR_NOERR 1
isr%1:
    push dword 0
    push dword %1
    jmp isr_common_stub
%endmacro

%macro ISR_ERR 1
isr%1:
    push dword %1
    jmp isr_common_stub
%endmacro


isr_common_stub:
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


    ; stack after:
    ;
    ; gs
    ; fs
    ; es
    ; ds
    ; edi
    ; esi
    ; ebp
    ; esp
    ; ebx
    ; edx
    ; ecx
    ; eax
    ; vector
    ; error_code
    ;

    mov eax, [esp + 48]     ; vector
    mov edx, [esp + 52]     ; error code

    push edx
    push eax

    call isr_common_handler

    add esp, 8


    pop gs
    pop fs
    pop es
    pop ds

    popa

    ; remove vector + error code
    add esp, 8

    iret


; --------------------------
; Exceptions
; --------------------------

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7

ISR_ERR   8

ISR_NOERR 9

ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14

ISR_NOERR 15
ISR_NOERR 16

ISR_ERR   17

ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29

ISR_ERR   30

ISR_NOERR 31


; IRQs / remaining vectors
%assign i 32
%rep 224
    ISR_NOERR i
%assign i i+1
%endrep


section .data

isr_stub_table:

%assign i 0
%rep 256
    dd isr %+ i
%assign i i+1
%endrep
