#include "ramfs_internal.h"
#include "recipes/status.h"
#include "userland_image.h"

ramfs_node ramfs_nodes[RAMFS_MAX_NODES];
size_t ramfs_node_count;
static ramfs_node write_backup;
static ramfs_node unlink_backup;

static size_t string_length(const char *s)
{
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

int ramfs_string_equal(const char *a, const char *b)
{
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static int is_embedded_program_path(const char *path)
{
    static const char prefix[] = "/system/bin/";
    if (ramfs_string_equal(path, "/system/bin")) return 1;
    size_t i = 0;
    while (prefix[i] && path[i] == prefix[i]) i++;
    return prefix[i] == '\0' && path[i] != '\0';
}

static void string_copy(char *dst, const char *src, size_t capacity)
{
    size_t i = 0;
    while (i + 1 < capacity && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

void ramfs_bytes_zero(void *buffer, size_t length)
{
    uint8_t *bytes = (uint8_t *)buffer;
    for (size_t i = 0; i < length; i++) bytes[i] = 0;
}

int ramfs_valid_path(const char *path)
{
    if (!path || path[0] != '/') return 0;
    if (string_length(path) >= RAMFS_PATH_MAX) return 0;

    for (size_t i = 0; path[i]; i++) {
        if (path[i] == '/' && path[i + 1] == '/') return 0;
        if (path[i] == '.' && path[i + 1] == '.' &&
            (i == 0 || path[i - 1] == '/') &&
            (path[i + 2] == '/' || path[i + 2] == '\0')) {
            return 0;
        }
    }
    return 1;
}

static ramfs_node *find_node(const char *path)
{
    for (size_t i = 0; i < ramfs_node_count; i++) {
        if (ramfs_string_equal(ramfs_nodes[i].path, path)) {
            return &ramfs_nodes[i];
        }
    }
    return 0;
}

static void remove_node_at(size_t index)
{
    for (size_t i = index + 1; i < ramfs_node_count; i++) {
        ramfs_nodes[i - 1] = ramfs_nodes[i];
    }
    ramfs_node_count--;
    ramfs_bytes_zero(&ramfs_nodes[ramfs_node_count], sizeof(ramfs_nodes[ramfs_node_count]));
}

static int is_legacy_help_page(const char *path)
{
    return ramfs_string_equal(path, "/README.txt") ||
           ramfs_string_equal(path, "/help/README.txt") ||
           ramfs_string_equal(path, "/help/CLEARSH.txt") ||
           ramfs_string_equal(path, "/help/BRAIN.txt") ||
           ramfs_string_equal(path, "/help/SYSTEM.txt");
}

static void migrate_legacy_help_paths(void)
{
    static const char help_prefix[] = "/help/";
    const size_t prefix_length = sizeof(help_prefix) - 1;

    for (size_t i = 0; i < ramfs_node_count;) {
        ramfs_node *node = &ramfs_nodes[i];
        if (is_legacy_help_page(node->path)) {
            remove_node_at(i);
            continue;
        }

        size_t prefix_index = 0;
        while (prefix_index < prefix_length &&
               node->path[prefix_index] == help_prefix[prefix_index]) {
            prefix_index++;
        }
        if (prefix_index == prefix_length) {
            char relocated_path[RAMFS_PATH_MAX];
            relocated_path[0] = '/';
            string_copy(relocated_path + 1,
                        node->path + prefix_length,
                        sizeof(relocated_path) - 1);
            if (find_node(relocated_path) ||
                !ramfs_parent_is_directory(relocated_path)) {
                remove_node_at(i);
                continue;
            }
            string_copy(node->path, relocated_path, sizeof(node->path));
        }
        i++;
    }

    for (size_t i = 0; i < ramfs_node_count; i++) {
        if (ramfs_string_equal(ramfs_nodes[i].path, "/help")) {
            remove_node_at(i);
            break;
        }
    }
}

static int add_node(const char *path, const char *data, size_t length, int directory)
{
    if (ramfs_node_count == RAMFS_MAX_NODES || length > RAMFS_FILE_MAX) {
        return 0;
    }

    ramfs_node *node = &ramfs_nodes[ramfs_node_count++];
    ramfs_bytes_zero(node, sizeof(*node));
    string_copy(node->path, path, sizeof(node->path));
    node->length = length;
    node->directory = directory;
    for (size_t i = 0; i < length; i++) {
        node->data[i] = data ? data[i] : 0;
    }
    return 1;
}

int ramfs_parent_is_directory(const char *path)
{
    size_t length = string_length(path);
    size_t slash = length;
    while (slash > 0 && path[slash - 1] != '/') slash--;
    if (slash == 0) return 0;

    char parent[RAMFS_PATH_MAX];
    size_t parent_length = slash == 1 ? 1 : slash - 1;
    for (size_t i = 0; i < parent_length; i++) {
        parent[i] = path[i];
    }
    parent[parent_length] = '\0';

    ramfs_node *node = find_node(parent);
    return node && node->directory;
}

lucid_status ramfs_pre(void)
{
    return LUCID_OK;
}

lucid_status ramfs_init(void)
{
    int partition = ramfs_storage_discover();
    if (partition < 0) {
        return LUCID_FAIL_EX(
            1,
            "Persistent filesystem partition is invalid",
            "the MBR Lucid partition has an invalid range",
            "recreate the disk image with ./build.sh disk"
        );
    }

    if (partition > 0) {
        int loaded = ramfs_storage_load();
        if (loaded > 0) {
            migrate_legacy_help_paths();
            for (size_t i = 0; i < ramfs_seed_count; i++) {
                if (!find_node(ramfs_seeds[i].path) &&
                    ramfs_parent_is_directory(ramfs_seeds[i].path)) {
                    if (!add_node(ramfs_seeds[i].path, ramfs_seeds[i].data,
                                  ramfs_seeds[i].length, ramfs_seeds[i].directory)) {
                        break;
                    }
                }
            }
            if (!ramfs_validate_nodes(ramfs_node_count)) {
                return LUCID_FAIL_EX(
                    1,
                    "Persistent filesystem migration failed",
                    "the root-level help paths are invalid",
                    "check the saved filesystem paths"
                );
            }
            return LUCID_OK_MSG("Persistent filesystem loaded from disk");
        }
        if (loaded < 0) {
            return LUCID_FAIL_EX(
                1,
                "Persistent filesystem is damaged",
                "both on-disk filesystem snapshots failed validation",
                "back up the disk before attempting recovery"
            );
        }
    }

    ramfs_bytes_zero(ramfs_nodes, sizeof(ramfs_nodes));
    ramfs_node_count = 0;
    for (size_t i = 0; i < ramfs_seed_count; i++) {
        if (!add_node(ramfs_seeds[i].path, ramfs_seeds[i].data,
                      ramfs_seeds[i].length, ramfs_seeds[i].directory)) {
            return LUCID_FAIL_EX(
                1,
                "RAM filesystem initialization failed",
                "seed entries exceed the filesystem capacity",
                "increase RAMFS_MAX_NODES or RAMFS_FILE_MAX"
            );
        }
    }
    if (!ramfs_validate_nodes(ramfs_node_count)) {
        return LUCID_FAIL_EX(
            1,
            "RAM filesystem seed data is invalid",
            "seed paths must be unique and have existing directory parents",
            "check the RAMFS entries in impl/ramfs/static/ramfs.c"
        );
    }

    if (ramfs_storage_enabled() && !ramfs_storage_sync()) {
        return LUCID_FAIL_EX(
            1,
            "Could not initialize persistent filesystem",
            "writing the initial snapshot to the Lucid disk partition failed",
            "check the disk image and primary ATA device"
        );
    }

    if (ramfs_storage_enabled()) {
        return LUCID_OK_MSG("Persistent filesystem initialized on disk");
    }
    return LUCID_OK_MSG("RAM filesystem initialized");
}

lucid_status ramfs_post(void)
{
    return LUCID_OK;
}

int ramfs_read(const char *path, char *buffer, size_t capacity, size_t *length)
{
    if (!ramfs_valid_path(path) || !buffer || !length) return RAMFS_INVALID_PATH;
    if (ramfs_info_is_dynamic(path)) {
        return ramfs_info_read(path, buffer, capacity, length);
    }
    ramfs_node *node = find_node(path);
    if (!node) return RAMFS_NOT_FOUND;
    if (node->directory) return ramfs_list(path, buffer, capacity, length);
    if (node->length > capacity) return RAMFS_TOO_LARGE;

    for (size_t i = 0; i < node->length; i++) {
        buffer[i] = node->data[i];
    }
    *length = node->length;
    return RAMFS_OK;
}

int ramfs_write(const char *path, const char *data, size_t length)
{
    if (!ramfs_valid_path(path) || !data) return RAMFS_INVALID_PATH;
    if (ramfs_info_is_dynamic(path)) return RAMFS_READ_ONLY;
    if (is_embedded_program_path(path)) return RAMFS_READ_ONLY;
    if (length > RAMFS_FILE_MAX) return RAMFS_TOO_LARGE;

    ramfs_node *node = find_node(path);
    int created = 0;
    if (!node) {
        if (!ramfs_parent_is_directory(path)) return RAMFS_NOT_FOUND;
        if (ramfs_node_count == RAMFS_MAX_NODES) return RAMFS_NO_SPACE;
        node = &ramfs_nodes[ramfs_node_count++];
        ramfs_bytes_zero(node, sizeof(*node));
        string_copy(node->path, path, sizeof(node->path));
        node->directory = 0;
        created = 1;
    } else if (node->directory) {
        return RAMFS_NOT_DIRECTORY;
    } else {
        write_backup = *node;
    }

    for (size_t i = 0; i < sizeof(node->data); i++) node->data[i] = 0;
    for (size_t i = 0; i < length; i++) {
        node->data[i] = data[i];
    }
    node->length = length;
    if (!ramfs_storage_sync()) {
        if (created) {
            ramfs_node_count--;
            ramfs_bytes_zero(node, sizeof(*node));
        } else {
            *node = write_backup;
        }
        return RAMFS_NO_SPACE;
    }
    return RAMFS_OK;
}

int ramfs_list(const char *path, char *buffer, size_t capacity, size_t *length)
{
    if (!ramfs_valid_path(path) || !buffer || !length) return RAMFS_INVALID_PATH;
    if (ramfs_string_equal(path, "/system/bin")) {
        return userland_image_list(buffer, capacity, length)
            ? RAMFS_TOO_LARGE
            : RAMFS_OK;
    }
    int include_embedded_bin =
        ramfs_string_equal(path, "/system") && !find_node("/system/bin");
    ramfs_node *directory = find_node(path);
    if (!directory) return RAMFS_NOT_FOUND;
    if (!directory->directory) return RAMFS_NOT_DIRECTORY;

    size_t path_length = string_length(path);
    size_t prefix_length = ramfs_string_equal(path, "/") ? 0 : path_length;
    size_t used = 0;
    for (size_t i = 0; i < ramfs_node_count; i++) {
        const char *child = ramfs_nodes[i].path;
        size_t j = 0;
        if (prefix_length == 0) {
            if (child[0] != '/') continue;
            j = 1;
        } else {
            while (j < prefix_length && child[j] == path[j]) j++;
            if (j != prefix_length || child[j] != '/') continue;
            j++;
        }

        const char *name = child + j;
        if (!*name) continue;
        size_t name_length = 0;
        while (name[name_length] && name[name_length] != '/') name_length++;
        if (name[name_length] == '/') continue;
        if (used + name_length + 1 > capacity) return RAMFS_TOO_LARGE;

        for (size_t k = 0; k < name_length; k++) {
            buffer[used++] = name[k];
        }
        buffer[used++] = '\n';
    }
    if (include_embedded_bin) {
        if (used + sizeof("bin\n") - 1 > capacity) return RAMFS_TOO_LARGE;
        buffer[used++] = 'b';
        buffer[used++] = 'i';
        buffer[used++] = 'n';
        buffer[used++] = '\n';
    }
    *length = used;
    return RAMFS_OK;
}

int ramfs_is_directory(const char *path)
{
    if (!ramfs_valid_path(path)) return -1;
    if (ramfs_string_equal(path, "/system/bin")) return 1;
    if (ramfs_info_is_dynamic(path) || userland_find_image(path)) return 0;

    ramfs_node *node = find_node(path);
    if (!node) return -1;
    return node->directory ? 1 : 0;
}

int ramfs_unlink(const char *path)
{
    if (!ramfs_valid_path(path)) return RAMFS_INVALID_PATH;
    if (ramfs_info_is_dynamic(path) || is_embedded_program_path(path)) {
        return RAMFS_READ_ONLY;
    }

    ramfs_node *node = find_node(path);
    if (!node) return RAMFS_NOT_FOUND;
    if (node->directory) return RAMFS_IS_DIRECTORY;

    size_t index = (size_t)(node - ramfs_nodes);
    unlink_backup = *node;
    remove_node_at(index);
    if (!ramfs_storage_sync()) {
        for (size_t i = ramfs_node_count; i > index; i--) {
            ramfs_nodes[i] = ramfs_nodes[i - 1];
        }
        ramfs_nodes[index] = unlink_backup;
        ramfs_node_count++;
        return RAMFS_NO_SPACE;
    }
    return RAMFS_OK;
}
