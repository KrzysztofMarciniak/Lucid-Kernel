#include "recipes/ramfs.h"
#include "recipes/ata.h"
#include "recipes/pmm.h"
#include "recipes/status.h"
#include "recipes/timer.h"
#include "recipes/usermode.h"
#include "userland_image.h"
#include "stdint.h"

#define RAMFS_MAX_NODES 64
#define RAMFS_PATH_MAX 256
#define RAMFS_FILE_MAX 4096
#define RAMFS_NODE_SECTORS 545u
#define RAMFS_SLOT_SECTORS (RAMFS_NODE_SECTORS + 1u)
#define RAMFS_STORAGE_SECTORS (RAMFS_SLOT_SECTORS * 2u)
#define RAMFS_DISK_VERSION 1u
#define RAMFS_PARTITION_TYPE 0xDAu
#define RAMFS_HEADER_SIZE 24u

#define RAMFS_OK 0
#define RAMFS_NOT_FOUND 1
#define RAMFS_NOT_DIRECTORY 2
#define RAMFS_TOO_LARGE 3
#define RAMFS_INVALID_PATH 4
#define RAMFS_NO_SPACE 5
#define RAMFS_READ_ONLY 6

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

typedef char ramfs_node_size_must_be_4360[
    sizeof(ramfs_node) == 4360 ? 1 : -1
];

static ramfs_node nodes[RAMFS_MAX_NODES];
static size_t node_count;
static ramfs_node write_backup;
static uint8_t sector_buffer[512];
static uint32_t storage_start;
static uint32_t storage_sectors;
static uint32_t storage_generation;
static int storage_enabled;
static int storage_active_slot = -1;

static int string_equal(const char *a, const char *b);
static int valid_path(const char *path);
static int parent_is_directory(const char *path);

static int is_dynamic_info(const char *path)
{
    return string_equal(path, "/system/info/uptime") ||
           string_equal(path, "/system/info/memory") ||
           string_equal(path, "/system/info/storage") ||
           string_equal(path, "/system/info/processes");
}

static int append_text(char *buffer, size_t capacity,
                       size_t *used, const char *text)
{
    for (size_t i = 0; text[i]; i++) {
        if (*used >= capacity) return 0;
        buffer[(*used)++] = text[i];
    }
    return 1;
}

static int append_number(char *buffer, size_t capacity,
                         size_t *used, uint32_t number)
{
    char digits[10];
    size_t count = 0;
    do {
        digits[count++] = (char)('0' + number % 10);
        number /= 10;
    } while (number && count < sizeof(digits));
    while (count) {
        if (*used >= capacity) return 0;
        buffer[(*used)++] = digits[--count];
    }
    return 1;
}

static int read_dynamic_info(const char *path,
                             char *buffer,
                             size_t capacity,
                             size_t *length)
{
    size_t used = 0;
    if (string_equal(path, "/system/info/uptime")) {
        uint32_t ticks = timer_get_ticks();
        if (!append_number(buffer, capacity, &used, ticks / 100) ||
            !append_text(buffer, capacity, &used, " seconds (") ||
            !append_number(buffer, capacity, &used, ticks) ||
            !append_text(buffer, capacity, &used, " ticks)\n")) {
            return RAMFS_TOO_LARGE;
        }
    } else if (string_equal(path, "/system/info/memory")) {
        uint32_t managed;
        uint32_t free_frames;
        uint32_t reserved;
        pmm_get_stats(&managed, &free_frames, &reserved);
        if (!append_text(buffer, capacity, &used, "managed: ") ||
            !append_number(buffer, capacity, &used, managed * 4) ||
            !append_text(buffer, capacity, &used, " KiB\nfree: ") ||
            !append_number(buffer, capacity, &used, free_frames * 4) ||
            !append_text(buffer, capacity, &used, " KiB\nreserved low: ") ||
            !append_number(buffer, capacity, &used, reserved * 4) ||
            !append_text(buffer, capacity, &used, " KiB\n")) {
            return RAMFS_TOO_LARGE;
        }
    } else if (string_equal(path, "/system/info/storage")) {
        const char *description = storage_enabled
            ? "Persistent LucidFS on ATA disk\n"
            : (ata_disk_present()
                ? "ATA disk present; no LucidFS partition (RAM-only)\n"
                : "RAM-only; no ATA disk detected\n");
        if (!append_text(buffer, capacity, &used, description)) {
            return RAMFS_TOO_LARGE;
        }
    } else if (string_equal(path, "/system/info/processes")) {
        return usermode_process_info(buffer, capacity, length)
            ? RAMFS_TOO_LARGE
            : RAMFS_OK;
    } else {
        return -1;
    }
    *length = used;
    return RAMFS_OK;
}

static size_t string_length(const char *s)
{
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

static int string_equal(const char *a, const char *b)
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
    if (string_equal(path, "/system/bin")) return 1;
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

static void bytes_zero(void *buffer, size_t length)
{
    uint8_t *bytes = (uint8_t *)buffer;
    for (size_t i = 0; i < length; i++) bytes[i] = 0;
}

static uint32_t read_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static void write_u32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static uint32_t checksum_nodes(size_t count)
{
    const uint8_t *bytes = (const uint8_t *)nodes;
    size_t length = count * sizeof(ramfs_node);
    uint32_t checksum = 2166136261u;
    for (size_t i = 0; i < length; i++) {
        checksum = (checksum ^ bytes[i]) * 16777619u;
    }
    return checksum;
}

static int find_storage_partition(void)
{
    storage_enabled = 0;
    storage_start = 0;
    storage_sectors = 0;
    if (!ata_disk_present() || !ata_read_sectors(0, 1, sector_buffer)) {
        return 0;
    }
    if (sector_buffer[510] != 0x55 || sector_buffer[511] != 0xAA) {
        return 0;
    }

    uint32_t disk_size = ata_disk_sector_count();
    for (size_t i = 0; i < 4; i++) {
        const uint8_t *entry = &sector_buffer[446 + i * 16];
        uint32_t start = read_u32(entry + 8);
        uint32_t count = read_u32(entry + 12);
        if (entry[4] != RAMFS_PARTITION_TYPE) continue;
        if (start == 0 || count < RAMFS_STORAGE_SECTORS ||
            start >= disk_size || count > disk_size - start) {
            return -1;
        }
        storage_start = start;
        storage_sectors = count;
        storage_enabled = 1;
        return 1;
    }
    return 0;
}

static int read_slot_header(size_t slot, ramfs_slot *result)
{
    uint32_t lba = storage_start + (uint32_t)slot * RAMFS_SLOT_SECTORS;
    if (!ata_read_sectors(lba, 1, sector_buffer)) return -1;

    static const uint8_t magic[8] = {
        'L', 'U', 'C', 'I', 'D', 'F', 'S', '1'
    };
    result->present = 0;
    for (size_t i = 0; i < sizeof(magic); i++) {
        if (sector_buffer[i] != magic[i]) return 0;
    }
    result->present = 1;
    if (read_u32(sector_buffer + 8) != RAMFS_DISK_VERSION) return -1;
    result->generation = read_u32(sector_buffer + 12);
    result->count = read_u32(sector_buffer + 16);
    result->checksum = read_u32(sector_buffer + 20);
    if (result->count == 0 || result->count > RAMFS_MAX_NODES) return -1;
    return 1;
}

static int valid_nodes(size_t count)
{
    if (count == 0 || count > RAMFS_MAX_NODES) return 0;
    node_count = count;
    if (!string_equal(nodes[0].path, "/") ||
        !nodes[0].directory || nodes[0].length != 0) {
        return 0;
    }

    for (size_t i = 0; i < count; i++) {
        size_t path_length = 0;
        while (path_length < sizeof(nodes[i].path) &&
               nodes[i].path[path_length]) {
            path_length++;
        }
        if (path_length == 0 || path_length == sizeof(nodes[i].path) ||
            !valid_path(nodes[i].path) ||
            nodes[i].length > RAMFS_FILE_MAX ||
            (nodes[i].directory != 0 && nodes[i].directory != 1) ||
            (nodes[i].directory && nodes[i].length != 0)) {
            return 0;
        }
        for (size_t j = 0; j < i; j++) {
            if (string_equal(nodes[i].path, nodes[j].path)) return 0;
        }
        if (i != 0 && !parent_is_directory(nodes[i].path)) return 0;
    }
    return 1;
}

static int load_slot(size_t slot, const ramfs_slot *header)
{
    uint32_t lba = storage_start +
                   (uint32_t)slot * RAMFS_SLOT_SECTORS + 1;
    if (!ata_read_sectors(lba, RAMFS_NODE_SECTORS, nodes)) return 0;
    if (checksum_nodes(header->count) != header->checksum) return 0;
    return valid_nodes(header->count);
}

static int generation_is_newer(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) > 0;
}

static int load_persistent_nodes(void)
{
    ramfs_slot slots[2];
    int states[2];
    bytes_zero(slots, sizeof(slots));
    states[0] = read_slot_header(0, &slots[0]);
    states[1] = read_slot_header(1, &slots[1]);

    int first = -1;
    int second = -1;
    if (states[0] > 0) first = 0;
    if (states[1] > 0) {
        if (first < 0 ||
            generation_is_newer(slots[1].generation,
                                slots[first].generation)) {
            second = first;
            first = 1;
        } else {
            second = 1;
        }
    }

    if (first >= 0 && load_slot((size_t)first, &slots[first])) {
        storage_active_slot = first;
        storage_generation = slots[first].generation;
        return 1;
    }
    if (second >= 0 && load_slot((size_t)second, &slots[second])) {
        storage_active_slot = second;
        storage_generation = slots[second].generation;
        return 1;
    }
    if (slots[0].present || slots[1].present) return -1;
    if (states[0] < 0 || states[1] < 0) return -1;
    return 0;
}

static int sync_persistent_nodes(void)
{
    if (!storage_enabled) return 1;
    if (storage_sectors < RAMFS_STORAGE_SECTORS) return 0;

    size_t slot = storage_active_slot < 0
        ? 0
        : (size_t)(storage_active_slot ^ 1);
    uint32_t lba = storage_start + (uint32_t)slot * RAMFS_SLOT_SECTORS;
    if (!ata_write_sectors(lba + 1, RAMFS_NODE_SECTORS, nodes)) return 0;

    bytes_zero(sector_buffer, sizeof(sector_buffer));
    sector_buffer[0] = 'L';
    sector_buffer[1] = 'U';
    sector_buffer[2] = 'C';
    sector_buffer[3] = 'I';
    sector_buffer[4] = 'D';
    sector_buffer[5] = 'F';
    sector_buffer[6] = 'S';
    sector_buffer[7] = '1';
    write_u32(sector_buffer + 8, RAMFS_DISK_VERSION);
    write_u32(sector_buffer + 12, storage_generation + 1);
    write_u32(sector_buffer + 16, (uint32_t)node_count);
    write_u32(sector_buffer + 20, checksum_nodes(node_count));
    if (!ata_write_sectors(lba, 1, sector_buffer)) return 0;

    storage_active_slot = (int)slot;
    storage_generation++;
    return 1;
}

static int valid_path(const char *path)
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
    for (size_t i = 0; i < node_count; i++) {
        if (string_equal(nodes[i].path, path)) {
            return &nodes[i];
        }
    }
    return 0;
}

static int add_node(const char *path, const char *data, size_t length, int directory)
{
    if (node_count == RAMFS_MAX_NODES || length > RAMFS_FILE_MAX) {
        return 0;
    }

    ramfs_node *node = &nodes[node_count++];
    bytes_zero(node, sizeof(*node));
    string_copy(node->path, path, sizeof(node->path));
    node->length = length;
    node->directory = directory;
    for (size_t i = 0; i < length; i++) {
        node->data[i] = data ? data[i] : 0;
    }
    return 1;
}

static int parent_is_directory(const char *path)
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
    static const struct {
        const char *path;
        const char *data;
        size_t length;
        int directory;
    } seeds[] = {
        { "/", 0, 0, 1 },
        { "/system", 0, 0, 1 },
        { "/system/info", 0, 0, 1 },
        { "/system/config", 0, 0, 1 },
        { "/system/config/pioneer", 0, 0, 1 },
        { "/system/config/brain.conf", "bar=top\n", 8, 0 },
        { "/users", 0, 0, 1 },
        { "/users/root", 0, 0, 1 },
        { "/tmp", 0, 0, 1 },
        { "/system/info/name", "Lucid\n", 6, 0 },
        { "/system/info/kernel", "Lucid Kernel\n", 13, 0 },
        { "/system/info/version", "0.1.0-pre\n", 10, 0 },
        { "/system/info/architecture", "x86\n", 4, 0 },
        { "/system/info/boot",
          "Kernel -> Pioneer -> Brain -> Eye -> Clearsh\n",
          sizeof("Kernel -> Pioneer -> Brain -> Eye -> Clearsh\n") - 1, 0 },
        { "/system/info/features",
          "ring3 user mode\nint 0x80 syscalls\nCOM1 and VGA console\n"
          "PS/2 keyboard\nRAMFS and ATA persistent LucidFS\n"
          "Brain tiled windows and ten desktops\n"
          "Eye terminal with Clearsh\nClearsh boot scripts\n",
          sizeof("ring3 user mode\nint 0x80 syscalls\nCOM1 and VGA console\n"
                 "PS/2 keyboard\nRAMFS and ATA persistent LucidFS\n"
                 "Brain tiled windows and ten desktops\n"
                 "Eye terminal with Clearsh\nClearsh boot scripts\n") - 1, 0 },
        { "/system/info/syscalls",
          "int 0x80: EAX=id, EBX/ECX/EDX/ESI=args; EAX=result\n"
          "0 write(ptr EBX,len ECX)\n1 exit(status EBX)\n"
          "2 readline(buffer EBX,capacity ECX,optional completion EDX/ESI)\n"
          "3 fs_read(path EBX,len ECX,out EDX,capacity ESI)\n"
          "4 fs_write(path EBX,len ECX,data EDX,len ESI)\n"
          "5 fs_list(path EBX,len ECX,out EDX,capacity ESI)\n"
          "6 readkey()  7 clear()  8 shutdown()  9 get_ticks()\n"
          "10 fork() 11 exec(path EBX,len ECX) 12 wait(pid EBX)\n"
          "13 reap() 14 terminal_size()  Brain events: "
          "0x300 open, 0x301 exit, 0x302 reap\n"
          "Failure result: 0xFFFFFFFF. See SYSCALLS.md in the source tree.\n",
          sizeof("int 0x80: EAX=id, EBX/ECX/EDX/ESI=args; EAX=result\n"
                 "0 write(ptr EBX,len ECX)\n1 exit(status EBX)\n"
                 "2 readline(buffer EBX,capacity ECX,optional completion EDX/ESI)\n"
                 "3 fs_read(path EBX,len ECX,out EDX,capacity ESI)\n"
                 "4 fs_write(path EBX,len ECX,data EDX,len ESI)\n"
                 "5 fs_list(path EBX,len ECX,out EDX,capacity ESI)\n"
                 "6 readkey()  7 clear()  8 shutdown()  9 get_ticks()\n"
                 "10 fork() 11 exec(path EBX,len ECX) 12 wait(pid EBX)\n"
                 "13 reap() 14 terminal_size()  Brain events: "
                 "0x300 open, 0x301 exit, 0x302 reap\n"
                 "Failure result: 0xFFFFFFFF. See SYSCALLS.md in the source tree.\n") - 1,
          0 },
        { "/system/info/uptime", "", 0, 0 },
        { "/system/info/memory", "", 0, 0 },
        { "/system/info/storage", "", 0, 0 },
        { "/system/info/processes", "", 0, 0 },
        { "/system/config/pioneer/boot",
          "welcome.clearsh\nsystem.clearsh\n",
          sizeof("welcome.clearsh\nsystem.clearsh\n") - 1, 0 },
        { "/system/config/pioneer/welcome.clearsh",
          "(print (text \"Pioneer startup scripts are active\"))\n",
          sizeof("(print (text \"Pioneer startup scripts are active\"))\n") - 1, 0 },
        { "/system/config/pioneer/system.clearsh",
          "(print (read (file /system/info/name)))\n",
          sizeof("(print (read (file /system/info/name)))\n") - 1, 0 },
        { "/users/root/welcome", "Welcome to Lucid.\n", 18, 0 }
    };

    int partition = find_storage_partition();
    if (partition < 0) {
        return LUCID_FAIL_EX(
            1,
            "Persistent filesystem partition is invalid",
            "the MBR Lucid partition has an invalid range",
            "recreate the disk image with ./build.sh disk"
        );
    }

    if (partition > 0) {
        int loaded = load_persistent_nodes();
        if (loaded > 0) {
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

    bytes_zero(nodes, sizeof(nodes));
    node_count = 0;
    for (size_t i = 0; i < sizeof(seeds) / sizeof(seeds[0]); i++) {
        if (!add_node(seeds[i].path, seeds[i].data,
                      seeds[i].length, seeds[i].directory)) {
            return LUCID_FAIL_EX(
                1,
                "RAM filesystem initialization failed",
                "seed entries exceed the filesystem capacity",
                "increase RAMFS_MAX_NODES or RAMFS_FILE_MAX"
            );
        }
    }
    if (!valid_nodes(node_count)) {
        return LUCID_FAIL_EX(
            1,
            "RAM filesystem seed data is invalid",
            "seed paths must be unique and have existing directory parents",
            "check the RAMFS entries in impl/ramfs/static/ramfs.c"
        );
    }

    if (storage_enabled && !sync_persistent_nodes()) {
        return LUCID_FAIL_EX(
            1,
            "Could not initialize persistent filesystem",
            "writing the initial snapshot to the Lucid disk partition failed",
            "check the disk image and primary ATA device"
        );
    }

    if (storage_enabled) {
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
    if (!valid_path(path) || !buffer || !length) return RAMFS_INVALID_PATH;
    if (is_dynamic_info(path)) {
        return read_dynamic_info(path, buffer, capacity, length);
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
    if (!valid_path(path) || !data) return RAMFS_INVALID_PATH;
    if (is_dynamic_info(path)) return RAMFS_READ_ONLY;
    if (is_embedded_program_path(path)) return RAMFS_READ_ONLY;
    if (length > RAMFS_FILE_MAX) return RAMFS_TOO_LARGE;

    ramfs_node *node = find_node(path);
    int created = 0;
    if (!node) {
        if (!parent_is_directory(path)) return RAMFS_NOT_FOUND;
        if (node_count == RAMFS_MAX_NODES) return RAMFS_NO_SPACE;
        node = &nodes[node_count++];
        bytes_zero(node, sizeof(*node));
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
    if (!sync_persistent_nodes()) {
        if (created) {
            node_count--;
            bytes_zero(node, sizeof(*node));
        } else {
            *node = write_backup;
        }
        return RAMFS_NO_SPACE;
    }
    return RAMFS_OK;
}

int ramfs_list(const char *path, char *buffer, size_t capacity, size_t *length)
{
    if (!valid_path(path) || !buffer || !length) return RAMFS_INVALID_PATH;
    if (string_equal(path, "/system/bin")) {
        return userland_image_list(buffer, capacity, length)
            ? RAMFS_TOO_LARGE
            : RAMFS_OK;
    }
    int include_embedded_bin =
        string_equal(path, "/system") && !find_node("/system/bin");
    ramfs_node *directory = find_node(path);
    if (!directory) return RAMFS_NOT_FOUND;
    if (!directory->directory) return RAMFS_NOT_DIRECTORY;

    size_t path_length = string_length(path);
    size_t prefix_length = string_equal(path, "/") ? 0 : path_length;
    size_t used = 0;
    for (size_t i = 0; i < node_count; i++) {
        const char *child = nodes[i].path;
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
