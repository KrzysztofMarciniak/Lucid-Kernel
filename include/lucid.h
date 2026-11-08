#ifndef LUCID_H
#define LUCID_H

#include <stdint.h>

uint32_t lucid_fs_read(const char *path, void *buffer, uint32_t capacity);
uint32_t lucid_fs_write(const char *path, const void *data, uint32_t length);

#endif
