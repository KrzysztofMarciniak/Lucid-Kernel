#ifndef LUCID_INCLUDE_MULTIBOOT_H
#define LUCID_INCLUDE_MULTIBOOT_H

#include "stdint.h"

#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002
#define MULTIBOOT_INFO_FRAMEBUFFER (1u << 12)
#define MULTIBOOT_FRAMEBUFFER_RGB 1

typedef struct __attribute__((packed)) multiboot_info {
    uint32_t flags;
    uint32_t reserved[21];
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t framebuffer_bpp;
    uint8_t framebuffer_type;
    uint8_t framebuffer_red_field_position;
    uint8_t framebuffer_red_mask_size;
    uint8_t framebuffer_green_field_position;
    uint8_t framebuffer_green_mask_size;
    uint8_t framebuffer_blue_field_position;
    uint8_t framebuffer_blue_mask_size;
} multiboot_info;

#endif
