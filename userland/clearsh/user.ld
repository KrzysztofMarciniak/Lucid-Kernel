ENTRY(clearsh_user_entry)

/* Link the shell in its dedicated user address range. */
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
    ASSERT(. <= 0x01100000, "Clearsh user image exceeds 1 MiB")
}
