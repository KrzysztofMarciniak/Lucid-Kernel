ENTRY(statusbar_user_entry)

SECTIONS
{
    . = 0x01000000;
    .text : { KEEP(*(.entry)) *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss (NOLOAD) : { *(COMMON) *(.bss*) }
}
