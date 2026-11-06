#ifndef LUCID_USERLAND_IMAGE_H
#define LUCID_USERLAND_IMAGE_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *path;
    const uint8_t *start;
    const uint8_t *end;
    uint32_t load_address;
    uint32_t memory_size;
} userland_image;

const userland_image *userland_initial_image(void);
const userland_image *userland_find_image(const char *path);
uint32_t userland_image_size(const userland_image *image);
int userland_image_list(char *buffer, size_t capacity, size_t *length);

#endif
