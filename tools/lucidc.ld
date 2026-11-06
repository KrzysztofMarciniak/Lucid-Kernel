ENTRY(_start)

SECTIONS
{
    . = 0x01000000;
    .text : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss (NOLOAD) : {
        *(COMMON)
        *(.bss*)
    }
    ASSERT(. <= 0x01100000, "LucidC image exceeds 1 MiB address space")
    /DISCARD/ : {
        *(.comment)
        *(.note*)
    }
}
