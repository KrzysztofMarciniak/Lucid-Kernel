ENTRY(brain_user_entry)

SECTIONS
{
    . = 0x01000000;
    .text : { *(.entry) *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss : { *(COMMON) *(.bss*) }
}
