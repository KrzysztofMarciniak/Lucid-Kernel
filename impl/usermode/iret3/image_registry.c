#include "userland_image.h"

#define USER_IMAGE_BASE 0x01000000u
#define USER_IMAGE_MEMORY_SIZE 0x00100000u

extern const uint8_t pioneer_image_start[];
extern const uint8_t pioneer_image_end[];
extern const uint8_t clearsh_image_start[];
extern const uint8_t clearsh_image_end[];

static const userland_image images[] = {
    {
        "/system/bin/pioneer",
        pioneer_image_start,
        pioneer_image_end,
        USER_IMAGE_BASE,
        USER_IMAGE_MEMORY_SIZE
    },
    {
        "/system/bin/clearsh",
        clearsh_image_start,
        clearsh_image_end,
        USER_IMAGE_BASE,
        USER_IMAGE_MEMORY_SIZE
    }
};

static int paths_equal(const char *left, const char *right)
{
    while (*left && *right && *left == *right) {
        left++;
        right++;
    }
    return *left == *right;
}

const userland_image *userland_initial_image(void)
{
    return &images[0];
}

const userland_image *userland_find_image(const char *path)
{
    if (!path) return 0;
    for (size_t i = 0; i < sizeof(images) / sizeof(images[0]); i++) {
        if (paths_equal(path, images[i].path)) return &images[i];
    }
    return 0;
}

int userland_image_list(char *buffer, size_t capacity, size_t *length)
{
    if (!buffer || !length) return 1;
    size_t used = 0;
    for (size_t i = 0; i < sizeof(images) / sizeof(images[0]); i++) {
        const char *name = images[i].path;
        for (const char *cursor = name; *cursor; cursor++) {
            if (*cursor == '/') name = cursor + 1;
        }
        while (*name) {
            if (used == capacity) return 1;
            buffer[used++] = *name++;
        }
        if (used == capacity) return 1;
        buffer[used++] = '\n';
    }
    *length = used;
    return 0;
}

uint32_t userland_image_size(const userland_image *image)
{
    if (!image || !image->start || !image->end) {
        return 0;
    }
    uintptr_t start = (uintptr_t)image->start;
    uintptr_t end = (uintptr_t)image->end;
    if (end < start) return 0;
    return (uint32_t)(end - start);
}
