#include "recipes/ata.h"
#include "recipes/terminal.h"

#define ATA_DATA        0x1F0
#define ATA_SECTOR_COUNT 0x1F2
#define ATA_LBA_LOW     0x1F3
#define ATA_LBA_MID     0x1F4
#define ATA_LBA_HIGH    0x1F5
#define ATA_DRIVE       0x1F6
#define ATA_STATUS      0x1F7
#define ATA_COMMAND     0x1F7

#define ATA_STATUS_ERR  0x01
#define ATA_STATUS_DRQ  0x08
#define ATA_STATUS_DF   0x20
#define ATA_STATUS_BSY  0x80

#define ATA_CMD_IDENTIFY 0xEC
#define ATA_CMD_READ     0x20
#define ATA_CMD_WRITE    0x30
#define ATA_MAX_CHUNK    128u
#define ATA_WAIT_LIMIT   1000000u

static int disk_present;
static uint8_t selected_drive;
static uint32_t disk_sectors;

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline uint16_t inw(uint16_t port)
{
    uint16_t value;
    __asm__ volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outw(uint16_t port, uint16_t value)
{
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

static void ata_write_hex8(uint8_t value)
{
    static const char digits[] = "0123456789ABCDEF";
    terminal_putc(digits[value >> 4]);
    terminal_putc(digits[value & 0x0F]);
}

static int ata_report_failure(const char *reason)
{
    uint8_t status = inb(ATA_STATUS);
    terminal_write("ATA I/O failure: ");
    terminal_write(reason);
    terminal_write(" (status 0x");
    ata_write_hex8(status);
    terminal_write(")\n");
    return 0;
}

static void ata_delay(void)
{
    (void)inb(ATA_STATUS);
    (void)inb(ATA_STATUS);
    (void)inb(ATA_STATUS);
    (void)inb(ATA_STATUS);
}

static int ata_wait(int need_data)
{
    for (uint32_t i = 0; i < ATA_WAIT_LIMIT; i++) {
        uint8_t status = inb(ATA_STATUS);
        if (status == 0 || status == 0xFF) return 0;
        if (status & (ATA_STATUS_ERR | ATA_STATUS_DF)) return 0;
        if (!(status & ATA_STATUS_BSY) &&
            (!need_data || (status & ATA_STATUS_DRQ))) {
            return 1;
        }
    }
    return 0;
}

static int ata_probe(uint8_t drive)
{
    uint16_t identify[256];
    outb(ATA_DRIVE, drive);
    ata_delay();
    outb(ATA_SECTOR_COUNT, 0);
    outb(ATA_LBA_LOW, 0);
    outb(ATA_LBA_MID, 0);
    outb(ATA_LBA_HIGH, 0);
    outb(ATA_COMMAND, ATA_CMD_IDENTIFY);

    uint8_t status = inb(ATA_STATUS);
    if (status == 0 || status == 0xFF || !ata_wait(0)) return 0;
    if (inb(ATA_LBA_MID) != 0 || inb(ATA_LBA_HIGH) != 0) return 0;
    if (!ata_wait(1)) return 0;

    for (uint32_t i = 0; i < 256; i++) identify[i] = inw(ATA_DATA);
    if (!(identify[49] & 0x0200)) return 0;

    disk_sectors = (uint32_t)identify[60] |
                   ((uint32_t)identify[61] << 16);
    if (disk_sectors == 0) return 0;
    selected_drive = drive;
    return 1;
}

lucid_status ata_pre(void)
{
    disk_present = 0;
    disk_sectors = 0;
    selected_drive = 0;
    return LUCID_OK;
}

lucid_status ata_init(void)
{
    if (ata_probe(0xE0) || ata_probe(0xF0)) {
        disk_present = 1;
        return LUCID_OK_MSG("Primary ATA disk detected (LBA28 PIO)");
    }
    return LUCID_OK_MSG("No primary ATA disk; storage remains volatile");
}

lucid_status ata_post(void)
{
    return LUCID_OK;
}

int ata_disk_present(void)
{
    return disk_present;
}

uint32_t ata_disk_sector_count(void)
{
    return disk_sectors;
}

static int ata_transfer(uint32_t lba, uint32_t count, uint8_t *buffer, int write)
{
    if (!disk_present || !buffer || count == 0 ||
        lba >= disk_sectors || count > disk_sectors - lba ||
        lba > 0x0FFFFFFFu || count > 0x10000000u - lba) {
        return ata_report_failure("invalid sector range");
    }

    while (count) {
        uint32_t chunk = count > ATA_MAX_CHUNK ? ATA_MAX_CHUNK : count;
        outb(ATA_DRIVE, (uint8_t)(selected_drive | 0x40 |
                                  ((lba >> 24) & 0x0F)));
        ata_delay();
        if (!ata_wait(0)) return ata_report_failure("device not ready");

        outb(ATA_SECTOR_COUNT, (uint8_t)chunk);
        outb(ATA_LBA_LOW, (uint8_t)lba);
        outb(ATA_LBA_MID, (uint8_t)(lba >> 8));
        outb(ATA_LBA_HIGH, (uint8_t)(lba >> 16));
        outb(ATA_COMMAND, write ? ATA_CMD_WRITE : ATA_CMD_READ);

        for (uint32_t sector = 0; sector < chunk; sector++) {
            if (!ata_wait(1)) return ata_report_failure("data request timed out");
            for (uint32_t word = 0; word < 256; word++) {
                if (write) {
                    uint16_t value = (uint16_t)buffer[word * 2] |
                                     ((uint16_t)buffer[word * 2 + 1] << 8);
                    outw(ATA_DATA, value);
                } else {
                    uint16_t value = inw(ATA_DATA);
                    buffer[word * 2] = (uint8_t)value;
                    buffer[word * 2 + 1] = (uint8_t)(value >> 8);
                }
            }
            buffer += 512;
            lba++;
        }
        if (!ata_wait(0)) return ata_report_failure("transfer did not complete");
        count -= chunk;
    }
    return 1;
}

int ata_read_sectors(uint32_t lba, uint32_t count, void *buffer)
{
    return ata_transfer(lba, count, (uint8_t *)buffer, 0);
}

int ata_write_sectors(uint32_t lba, uint32_t count, const void *buffer)
{
    return ata_transfer(lba, count, (uint8_t *)buffer, 1);
}
