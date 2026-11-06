#include "recipes/acpi.h"
#include "stdint.h"
#include "stddef.h"

#define ACPI_HEADER_SIZE 36u
#define ACPI_MAX_TABLE_SIZE (1024u * 1024u)
#define ACPI_MAX_RSDP_SIZE 4096u
#define ACPI_PM1_SCI_EN 0x0001u
#define ACPI_PM1_SLP_EN 0x2000u

typedef volatile const uint8_t acpi_byte;

static uint16_t pm1a_control_port;
static uint16_t pm1b_control_port;
static uint16_t smi_command_port;
static uint8_t acpi_enable_value;
static uint8_t sleep_type_a;
static uint8_t sleep_type_b;
static int acpi_poweroff_ready;

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

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline void outw(uint16_t port, uint16_t value)
{
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

static uint16_t read_u16(acpi_byte *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t read_u32(acpi_byte *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static uint64_t read_u64(acpi_byte *data)
{
    if (read_u32(data + 4) != 0) return 0x100000000ULL;
    return read_u32(data);
}

static int signature_is(acpi_byte *data, const char *signature)
{
    for (size_t i = 0; i < 4; i++) {
        if (data[i] != (uint8_t)signature[i]) return 0;
    }
    return 1;
}

static int checksum_valid(acpi_byte *data, size_t length)
{
    uint8_t sum = 0;
    for (size_t i = 0; i < length; i++) sum = (uint8_t)(sum + data[i]);
    return sum == 0;
}

static uint32_t find_rsdp_in_range(uint32_t start, uint32_t end)
{
    for (uint32_t address = start; address + 20u <= end; address += 16u) {
        acpi_byte *candidate = (acpi_byte *)address;
        if (candidate[0] != 'R' || candidate[1] != 'S' ||
            candidate[2] != 'D' || candidate[3] != ' ' ||
            candidate[4] != 'P' || candidate[5] != 'T' ||
            candidate[6] != 'R' || candidate[7] != ' ') {
            continue;
        }
        if (!checksum_valid(candidate, 20)) continue;
        if (candidate[15] >= 2) {
            uint32_t length = read_u32(candidate + 20);
            if (length < 36 || length > ACPI_MAX_RSDP_SIZE ||
                address + length > 0x00100000u ||
                !checksum_valid(candidate, length)) {
                continue;
            }
        }
        return address;
    }
    return 0;
}

static uint32_t find_rsdp(void)
{
    uint16_t ebda_segment = read_u16((acpi_byte *)0x40Eu);
    uint32_t ebda = (uint32_t)ebda_segment << 4;
    if (ebda >= 0x80000u && ebda < 0xA0000u) {
        uint32_t found = find_rsdp_in_range(ebda, ebda + 1024u);
        if (found) return found;
    }
    return find_rsdp_in_range(0xE0000u, 0x100000u);
}

static int valid_table(uint32_t address,
                      const char *signature,
                      acpi_byte **out_table,
                      uint32_t *out_length)
{
    if (!address || address > 0xFFFFF000u) return 0;
    acpi_byte *table = (acpi_byte *)address;
    uint32_t length = read_u32(table + 4);
    if (length < ACPI_HEADER_SIZE || length > ACPI_MAX_TABLE_SIZE ||
        length > 0xFFFFFFFFu - address ||
        !signature_is(table, signature) ||
        !checksum_valid(table, length)) {
        return 0;
    }
    if (out_table) *out_table = table;
    if (out_length) *out_length = length;
    return 1;
}

static uint32_t find_fadt(uint32_t root_address,
                          const char *root_signature)
{
    acpi_byte *root;
    uint32_t length;
    if (!valid_table(root_address, root_signature, &root, &length)) {
        return 0;
    }
    uint32_t entry_size = signature_is(root, "XSDT") ? 8u : 4u;
    if ((length - ACPI_HEADER_SIZE) % entry_size != 0) return 0;

    for (uint32_t offset = ACPI_HEADER_SIZE;
         offset + entry_size <= length;
         offset += entry_size) {
        uint64_t address = entry_size == 8
            ? read_u64(root + offset)
            : read_u32(root + offset);
        if (!address || address > 0xFFFFFFFFu) continue;
        if (valid_table((uint32_t)address, "FACP", 0, 0)) {
            return (uint32_t)address;
        }
    }
    return 0;
}

static uint32_t get_io_port(acpi_byte *fadt,
                            uint32_t fadt_length,
                            uint32_t legacy_offset,
                            uint32_t gas_offset)
{
    uint32_t port = read_u32(fadt + legacy_offset);
    if (port) return port <= 0xFFFFu ? port : 0;
    if (fadt_length < gas_offset + 12u) return 0;

    acpi_byte *gas = fadt + gas_offset;
    if (gas[0] != 1 || gas[1] < 16 || gas[2] != 0) return 0;
    uint64_t address = read_u64(gas + 4);
    return address && address <= 0xFFFFu ? (uint32_t)address : 0;
}

static int parse_pkg_length(acpi_byte *aml,
                            size_t limit,
                            size_t *offset,
                            uint32_t *length)
{
    if (*offset >= limit) return 0;
    uint8_t lead = aml[(*offset)++];
    uint8_t follow = lead >> 6;
    uint32_t value = follow ? (lead & 0x0Fu) : (lead & 0x3Fu);
    if (*offset + follow > limit) return 0;
    for (uint8_t i = 0; i < follow; i++) {
        value |= (uint32_t)aml[(*offset)++] << (4 + 8 * i);
    }
    *length = value;
    return 1;
}

static int parse_aml_integer(acpi_byte *aml,
                             size_t limit,
                             size_t *offset,
                             uint64_t *value)
{
    if (*offset >= limit) return 0;
    uint8_t opcode = aml[(*offset)++];
    switch (opcode) {
    case 0x00:
        *value = 0;
        return 1;
    case 0x01:
        *value = 1;
        return 1;
    case 0xFF:
        *value = ~(uint64_t)0;
        return 1;
    case 0x0A:
        if (*offset + 1 > limit) return 0;
        *value = aml[(*offset)++];
        return 1;
    case 0x0B:
        if (*offset + 2 > limit) return 0;
        *value = read_u16(aml + *offset);
        *offset += 2;
        return 1;
    case 0x0C:
        if (*offset + 4 > limit) return 0;
        *value = read_u32(aml + *offset);
        *offset += 4;
        return 1;
    case 0x0E:
        if (*offset + 8 > limit) return 0;
        *value = read_u64(aml + *offset);
        *offset += 8;
        return 1;
    default:
        return 0;
    }
}

static int find_sleep_types(acpi_byte *dsdt,
                            uint32_t dsdt_length,
                            uint8_t *type_a,
                            uint8_t *type_b)
{
    acpi_byte *aml = dsdt + ACPI_HEADER_SIZE;
    size_t aml_length = dsdt_length - ACPI_HEADER_SIZE;
    for (size_t i = 0; i < aml_length; i++) {
        if (aml[i] != 0x08) continue;
        size_t name = i + 1;
        while (name < aml_length &&
               (aml[name] == 0x5C || aml[name] == 0x5E)) {
            name++;
        }
        if (name + 5 > aml_length ||
            aml[name] != '_' || aml[name + 1] != 'S' ||
            aml[name + 2] != '5' || aml[name + 3] != '_') {
            continue;
        }
        size_t package_opcode = name + 4;
        if (aml[package_opcode] != 0x12) continue;

        size_t package_length_offset = package_opcode + 1;
        size_t cursor = package_length_offset;
        uint32_t package_length;
        if (!parse_pkg_length(aml, aml_length, &cursor,
                              &package_length)) {
            continue;
        }
        size_t package_end = package_length_offset + package_length;
        if (package_end > aml_length || cursor >= package_end) continue;
        uint8_t elements = aml[cursor++];
        uint64_t first;
        uint64_t second;
        if (elements < 2 ||
            !parse_aml_integer(aml, package_end, &cursor, &first) ||
            !parse_aml_integer(aml, package_end, &cursor, &second) ||
            first > 7 || second > 7) {
            continue;
        }
        *type_a = (uint8_t)first;
        *type_b = (uint8_t)second;
        return 1;
    }
    return 0;
}

static int configure_poweroff(void)
{
    uint32_t rsdp_address = find_rsdp();
    if (!rsdp_address) return 0;
    acpi_byte *rsdp = (acpi_byte *)rsdp_address;
    uint32_t fadt_address = 0;
    if (rsdp[15] >= 2) {
        uint64_t xsdt_address = read_u64(rsdp + 24);
        if (xsdt_address && xsdt_address <= 0xFFFFFFFFu) {
            fadt_address = find_fadt(
                (uint32_t)xsdt_address, "XSDT"
            );
        }
    }
    if (!fadt_address) {
        fadt_address = find_fadt(read_u32(rsdp + 16), "RSDT");
    }
    acpi_byte *fadt;
    uint32_t fadt_length;
    if (!fadt_address ||
        !valid_table(fadt_address, "FACP", &fadt, &fadt_length) ||
        fadt_length < 90 || fadt[89] < 2) {
        return 0;
    }

    uint32_t port_a = get_io_port(fadt, fadt_length, 64, 172);
    uint32_t port_b = get_io_port(fadt, fadt_length, 68, 184);
    uint32_t dsdt_address = read_u32(fadt + 40);
    if (fadt_length >= 148) {
        uint64_t extended_dsdt = read_u64(fadt + 140);
        if (extended_dsdt && extended_dsdt <= 0xFFFFFFFFu) {
            dsdt_address = (uint32_t)extended_dsdt;
        }
    }
    acpi_byte *dsdt;
    uint32_t dsdt_length;
    uint8_t type_a;
    uint8_t type_b;
    if (!port_a || !dsdt_address ||
        !valid_table(dsdt_address, "DSDT", &dsdt, &dsdt_length) ||
        !find_sleep_types(dsdt, dsdt_length, &type_a, &type_b)) {
        return 0;
    }

    pm1a_control_port = (uint16_t)port_a;
    pm1b_control_port = port_b <= 0xFFFFu ? (uint16_t)port_b : 0;
    smi_command_port = read_u32(fadt + 48) <= 0xFFFFu
        ? (uint16_t)read_u32(fadt + 48)
        : 0;
    acpi_enable_value = fadt[52];
    sleep_type_a = type_a;
    sleep_type_b = type_b;
    acpi_poweroff_ready = 1;
    return 1;
}

lucid_status acpi_pre(void)
{
    return LUCID_OK;
}

lucid_status acpi_init(void)
{
    if (configure_poweroff()) {
        return LUCID_OK_MSG("ACPI S5 poweroff ready");
    }
    return LUCID_OK_MSG("ACPI S5 unavailable; using legacy poweroff ports");
}

lucid_status acpi_post(void)
{
    return LUCID_OK;
}

void acpi_poweroff(void)
{
    if (acpi_poweroff_ready) {
        if (smi_command_port && acpi_enable_value &&
            !(inw(pm1a_control_port) & ACPI_PM1_SCI_EN)) {
            outb(smi_command_port, acpi_enable_value);
            for (uint32_t i = 0; i < 1000000u; i++) {
                if (inw(pm1a_control_port) & ACPI_PM1_SCI_EN) break;
            }
        }

        uint16_t value_a =
            ((uint16_t)sleep_type_a << 10) | ACPI_PM1_SLP_EN;
        outw(pm1a_control_port, value_a);
        if (pm1b_control_port) {
            uint16_t value_b =
                ((uint16_t)sleep_type_b << 10) | ACPI_PM1_SLP_EN;
            outw(pm1b_control_port, value_b);
        }
    }

    outw(0x0604, 0x2000);
    outw(0xB004, 0x2000);
}
