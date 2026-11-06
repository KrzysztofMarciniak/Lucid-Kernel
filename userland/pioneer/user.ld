ENTRY(pioneer_user_entry)

SECTIONS
{
    . = 0x01000000;
    .text : {
        KEEP(*(.entry))
        *(.text*)
    }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss (NOLOAD) : {
        *(COMMON)
        *(.bss*)
    }
    ASSERT(. <= 0x01100000, "Pioneer user image exceeds 1 MiB")
}
