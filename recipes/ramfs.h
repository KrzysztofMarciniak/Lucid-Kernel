#ifndef LUCID_RECIPE_RAMFS_H
#define LUCID_RECIPE_RAMFS_H

/* REQUIRES: ata, timer, pmm, usermode */
#include "status.h"
#include <stddef.h>

lucid_status ramfs_pre(void);
lucid_status ramfs_init(void);
lucid_status ramfs_post(void);

int ramfs_read(const char *path, char *buffer, size_t capacity, size_t *length);
int ramfs_write(const char *path, const char *data, size_t length);
int ramfs_list(const char *path, char *buffer, size_t capacity, size_t *length);

#endif
