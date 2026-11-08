#ifndef LUCID_RECIPE_RAMFS_H
#define LUCID_RECIPE_RAMFS_H

/* REQUIRES: ata, timer, pmm, usermode */
#include "status.h"
#include <stddef.h>

#define RAMFS_OK 0
#define RAMFS_NOT_FOUND 1
#define RAMFS_NOT_DIRECTORY 2
#define RAMFS_TOO_LARGE 3
#define RAMFS_INVALID_PATH 4
#define RAMFS_NO_SPACE 5
#define RAMFS_READ_ONLY 6
#define RAMFS_IS_DIRECTORY 7

lucid_status ramfs_pre(void);
lucid_status ramfs_init(void);
lucid_status ramfs_post(void);

int ramfs_read(const char *path, char *buffer, size_t capacity, size_t *length);
int ramfs_write(const char *path, const char *data, size_t length);
int ramfs_list(const char *path, char *buffer, size_t capacity, size_t *length);
int ramfs_is_directory(const char *path);
int ramfs_unlink(const char *path);

#endif
