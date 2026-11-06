global clearsh_image_start
global clearsh_image_end

section .rodata
align 16
clearsh_image_start:
    incbin ".build/clearsh.bin"
clearsh_image_end:
