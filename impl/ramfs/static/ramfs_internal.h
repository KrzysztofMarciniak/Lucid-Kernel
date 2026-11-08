#ifndef LUCID_IMPL_RAMFS_INTERNAL_H
#define LUCID_IMPL_RAMFS_INTERNAL_H

#include "recipes/ramfs.h"
#include "stdint.h"

#define RAMFS_MAX_NODES 64
#define RAMFS_PATH_MAX 256
#define RAMFS_FILE_MAX 4096
#define RAMFS_NODE_SECTORS 545u
#define RAMFS_SLOT_SECTORS (RAMFS_NODE_SECTORS + 1u)
#define RAMFS_STORAGE_SECTORS (RAMFS_SLOT_SECTORS * 2u)
#define RAMFS_DISK_VERSION 1u
#define RAMFS_PARTITION_TYPE 0xDAu

typedef struct {
    char path[RAMFS_PATH_MAX];
    char data[RAMFS_FILE_MAX];
    size_t length;
    int directory;
} ramfs_node;

typedef struct {
    uint32_t generation;
    uint32_t count;
    uint32_t checksum;
    int present;
} ramfs_slot;

typedef struct {
    const char *path;
    const char *data;
    size_t length;
    int directory;
} ramfs_seed_entry;

typedef char ramfs_node_size_must_be_4360[
    sizeof(ramfs_node) == 4360 ? 1 : -1
];

extern const ramfs_seed_entry ramfs_seeds[];
extern const size_t ramfs_seed_count;

extern ramfs_node ramfs_nodes[RAMFS_MAX_NODES];
extern size_t ramfs_node_count;

int ramfs_string_equal(const char *a, const char *b);
int ramfs_valid_path(const char *path);
int ramfs_parent_is_directory(const char *path);
int ramfs_validate_nodes(size_t count);
void ramfs_bytes_zero(void *buffer, size_t length);

int ramfs_storage_discover(void);
int ramfs_storage_load(void);
int ramfs_storage_sync(void);
int ramfs_storage_enabled(void);

int ramfs_info_is_dynamic(const char *path);
int ramfs_info_read(const char *path,
                    char *buffer,
                    size_t capacity,
                    size_t *length);

#endif
