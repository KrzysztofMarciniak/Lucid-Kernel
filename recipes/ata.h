#ifndef LUCID_RECIPE_ATA_H
#define LUCID_RECIPE_ATA_H

#include "status.h"
#include <stdint.h>

lucid_status ata_pre(void);
lucid_status ata_init(void);
lucid_status ata_post(void);

int ata_disk_present(void);
uint32_t ata_disk_sector_count(void);
int ata_read_sectors(uint32_t lba, uint32_t count, void *buffer);
int ata_write_sectors(uint32_t lba, uint32_t count, const void *buffer);

#endif
