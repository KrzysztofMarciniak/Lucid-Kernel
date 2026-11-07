#include "ramfs_internal.h"
#include "recipes/ata.h"
#include "recipes/pmm.h"
#include "recipes/timer.h"
#include "recipes/usermode.h"

int ramfs_info_is_dynamic(const char *path)
{
    return ramfs_string_equal(path, "/system/info/uptime") ||
           ramfs_string_equal(path, "/system/info/memory") ||
           ramfs_string_equal(path, "/system/info/storage") ||
           ramfs_string_equal(path, "/system/info/processes");
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

int ramfs_info_read(const char *path,
                    char *buffer,
                    size_t capacity,
                    size_t *length)
{
    size_t used = 0;
    if (ramfs_string_equal(path, "/system/info/uptime")) {
        uint32_t ticks = timer_get_ticks();
        if (!append_number(buffer, capacity, &used, ticks / 100) ||
            !append_text(buffer, capacity, &used, " seconds (") ||
            !append_number(buffer, capacity, &used, ticks) ||
            !append_text(buffer, capacity, &used, " ticks)\n")) {
            return RAMFS_TOO_LARGE;
        }
    } else if (ramfs_string_equal(path, "/system/info/memory")) {
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
    } else if (ramfs_string_equal(path, "/system/info/storage")) {
        const char *description = ramfs_storage_enabled()
            ? "Persistent LucidFS on ATA disk\n"
            : (ata_disk_present()
                ? "ATA disk present; no LucidFS partition (RAM-only)\n"
                : "RAM-only; no ATA disk detected\n");
        if (!append_text(buffer, capacity, &used, description)) {
            return RAMFS_TOO_LARGE;
        }
    } else if (ramfs_string_equal(path, "/system/info/processes")) {
        return usermode_process_info(buffer, capacity, length)
            ? RAMFS_TOO_LARGE
            : RAMFS_OK;
    } else {
        return RAMFS_NOT_FOUND;
    }
    *length = used;
    return RAMFS_OK;
}
