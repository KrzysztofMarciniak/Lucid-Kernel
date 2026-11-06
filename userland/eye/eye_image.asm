global eye_image_start
global eye_image_end

section .rodata
align 16
eye_image_start:
    incbin ".build/eye.bin"
eye_image_end:
