global brain_image_start
global brain_image_end

section .rodata
align 16
brain_image_start:
    incbin ".build/brain.bin"
brain_image_end:
