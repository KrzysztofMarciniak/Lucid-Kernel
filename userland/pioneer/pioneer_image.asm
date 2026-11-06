global pioneer_image_start
global pioneer_image_end

section .rodata
align 16
pioneer_image_start:
    incbin ".build/pioneer.bin"
pioneer_image_end:
