#include "ramfs_internal.h"
#include "recipes/ata.h"

static uint8_t sector_buffer[512];
static uint32_t storage_start;
static uint32_t storage_sectors;
static uint32_t storage_generation;
static int storage_enabled;
static int storage_active_slot = -1;

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
    const uint8_t *bytes = (const uint8_t *)ramfs_nodes;
    size_t length = count * sizeof(ramfs_node);
    uint32_t checksum = 2166136261u;
    for (size_t i = 0; i < length; i++) {
        checksum = (checksum ^ bytes[i]) * 16777619u;
    }
    return checksum;
}

int ramfs_storage_discover(void)
{
    storage_enabled = 0;
    storage_start = 0;
    storage_sectors = 0;
    storage_generation = 0;
    storage_active_slot = -1;
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

int ramfs_validate_nodes(size_t count)
{
    if (count == 0 || count > RAMFS_MAX_NODES) return 0;
    ramfs_node_count = count;
    if (!ramfs_string_equal(ramfs_nodes[0].path, "/") ||
        !ramfs_nodes[0].directory || ramfs_nodes[0].length != 0) {
        return 0;
    }

    for (size_t i = 0; i < count; i++) {
        size_t path_length = 0;
        while (path_length < sizeof(ramfs_nodes[i].path) &&
               ramfs_nodes[i].path[path_length]) {
            path_length++;
        }
        if (path_length == 0 || path_length == sizeof(ramfs_nodes[i].path) ||
            !ramfs_valid_path(ramfs_nodes[i].path) ||
            ramfs_nodes[i].length > RAMFS_FILE_MAX ||
            (ramfs_nodes[i].directory != 0 && ramfs_nodes[i].directory != 1) ||
            (ramfs_nodes[i].directory && ramfs_nodes[i].length != 0)) {
            return 0;
        }
        for (size_t j = 0; j < i; j++) {
            if (ramfs_string_equal(ramfs_nodes[i].path, ramfs_nodes[j].path)) return 0;
        }
        if (i != 0 && !ramfs_parent_is_directory(ramfs_nodes[i].path)) return 0;
    }
    return 1;
}

static int load_slot(size_t slot, const ramfs_slot *header)
{
    uint32_t lba = storage_start +
                   (uint32_t)slot * RAMFS_SLOT_SECTORS + 1;
    if (!ata_read_sectors(lba, RAMFS_NODE_SECTORS, ramfs_nodes)) return 0;
    if (checksum_nodes(header->count) != header->checksum) return 0;
    return ramfs_validate_nodes(header->count);
}

static int generation_is_newer(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) > 0;
}

int ramfs_storage_load(void)
{
    ramfs_slot slots[2];
    int states[2];
    ramfs_bytes_zero(slots, sizeof(slots));
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

int ramfs_storage_sync(void)
{
    if (!storage_enabled) return 1;
    if (storage_sectors < RAMFS_STORAGE_SECTORS) return 0;

    size_t slot = storage_active_slot < 0
        ? 0
        : (size_t)(storage_active_slot ^ 1);
    uint32_t lba = storage_start + (uint32_t)slot * RAMFS_SLOT_SECTORS;
    if (!ata_write_sectors(lba + 1, RAMFS_NODE_SECTORS, ramfs_nodes)) return 0;

    ramfs_bytes_zero(sector_buffer, sizeof(sector_buffer));
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
    write_u32(sector_buffer + 16, (uint32_t)ramfs_node_count);
    write_u32(sector_buffer + 20, checksum_nodes(ramfs_node_count));
    if (!ata_write_sectors(lba, 1, sector_buffer)) return 0;

    storage_active_slot = (int)slot;
    storage_generation++;
    return 1;
}

int ramfs_storage_enabled(void)
{
    return storage_enabled;
}
