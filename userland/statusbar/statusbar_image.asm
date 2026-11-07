global statusbar_image_start
global statusbar_image_end

section .rodata
align 16
statusbar_image_start:
    incbin ".build/statusbar.bin"
statusbar_image_end:
