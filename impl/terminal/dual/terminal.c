#include "recipes/terminal.h"
#include "recipes/ramfs.h"
#include "recipes/status.h"
#include "recipes/usermode.h"
#include "recipes/vmm.h"
#include "terminal_io.h"
#include "stdint.h"
#include "stddef.h"
#include "multiboot.h"

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define CURIE_WIDTH 6
#define CURIE_HEIGHT 12
#define TERMINAL_ZOOM_MAX 4
#define TERMINAL_STATUS_MAX 64
#define TERMINAL_STATUSBAR_PATH_MAX 256
#define TERMINAL_STATUSBAR_TEXT_MAX 128
#define TERMINAL_BAR_WIDTH 14
#define VBE_CONTROL_INFO_SIZE 512u
#define VBE_MODE_LIST_MAX 1024u
#define VBE_MODE_INFO_ADDRESS 0x1F000u
#define VBE_TRAMPOLINE_ADDRESS 0x30000u
#define VBE_SCRATCH_ADDRESS 0x1E000u
#define VBE_TRAMPOLINE_LIMIT 0x10000u

typedef struct __attribute__((packed)) {
    uint16_t attributes;
    uint8_t reserved0[14];
    uint16_t bytes_per_scan_line;
    uint16_t x_resolution;
    uint16_t y_resolution;
    uint8_t reserved1[3];
    uint8_t bits_per_pixel;
    uint8_t reserved2;
    uint8_t memory_model;
    uint8_t reserved3[3];
    uint8_t red_mask_size;
    uint8_t red_field_position;
    uint8_t green_mask_size;
    uint8_t green_field_position;
    uint8_t blue_mask_size;
    uint8_t blue_field_position;
    uint8_t reserved_mask_size;
    uint8_t reserved_field_position;
    uint8_t direct_color_info;
    uint32_t physical_base;
} vbe_mode_info;

extern uint8_t vbe_trampoline_start[];
extern uint8_t vbe_trampoline_end[];

typedef enum {
    TERMINAL_BAR_TOP,
    TERMINAL_BAR_BOTTOM,
    TERMINAL_BAR_LEFT,
    TERMINAL_BAR_RIGHT
} terminal_bar_position;

typedef struct {
    char cells[TERMINAL_MAX_ROWS][TERMINAL_MAX_COLUMNS];
    uint32_t row;
    uint32_t column;
    uint32_t columns;
    uint32_t rows;
    uint32_t csi_parameter;
    uint32_t csi_second_parameter;
    int lambda_prefix;
    int escape_state;
    int wrap_pending;
    int csi_second_parameter_active;
} terminal_surface;

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    int title;
    int visible;
} terminal_window;

static volatile uint16_t *const vga_buf = (uint16_t *)0xB8000;
extern const uint8_t curie_font[128][CURIE_HEIGHT];
static volatile uint8_t *framebuffer;
static uint32_t framebuffer_phys;
static uint32_t framebuffer_length;
static uint32_t framebuffer_pitch;
static uint32_t framebuffer_width;
static uint32_t framebuffer_height;
static uint32_t framebuffer_foreground = 0xFFFFFFFFu;
static uint32_t framebuffer_background = 0xFF000000u;
static uint32_t framebuffer_accent = 0xFF00FF00u;
static uint32_t vbe_control_info;
static const char *framebuffer_status =
    "GRUB did not provide a framebuffer; using VGA output";
static terminal_surface terminal_surfaces[USERMODE_PROCESS_SLOTS];
static terminal_window terminal_windows[USERMODE_PROCESS_SLOTS];
static terminal_bar_position terminal_bar = TERMINAL_BAR_TOP;
static uint32_t terminal_zoom = 1;
static int terminal_config_loaded;
static char terminal_status[TERMINAL_STATUS_MAX] = "Ready";

static uint16_t read_u16(const volatile uint8_t *bytes)
{
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

static uint32_t read_u32(const volatile uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static uint16_t vbe_bios_call(uint16_t function,
                             uint16_t ebx,
                             uint16_t ecx)
{
    size_t code_length = (size_t)(vbe_trampoline_end -
                                  vbe_trampoline_start);
    if (code_length == 0 || code_length >= VBE_TRAMPOLINE_LIMIT) {
        return 0;
    }

    volatile uint8_t *destination =
        (volatile uint8_t *)VBE_TRAMPOLINE_ADDRESS;
    for (size_t i = 0; i < code_length; i++) {
        destination[i] = vbe_trampoline_start[i];
    }

    typedef uint32_t (*vbe_call_fn)(uint32_t, uint32_t, uint32_t);
    vbe_call_fn call = (vbe_call_fn)VBE_TRAMPOLINE_ADDRESS;
    return (uint16_t)call(function, ebx, ecx);
}

static int ranges_overlap(uint32_t first,
                          uint32_t first_length,
                          uint32_t second,
                          uint32_t second_length)
{
    uint64_t first_end = (uint64_t)first + first_length;
    uint64_t second_end = (uint64_t)second + second_length;
    return first < second_end && second < first_end;
}

static int parse_resolution(const char *text,
                            size_t length,
                            uint32_t *width,
                            uint32_t *height)
{
    size_t end = length;
    while (end && (text[end - 1] == '\n' || text[end - 1] == '\r')) end--;

    uint32_t values[3] = { 0, 0, 0 };
    size_t field = 0;
    for (size_t i = 0; i < end; i++) {
        char c = text[i];
        if (c == 'x' && field < 2 && values[field]) {
            field++;
            continue;
        }
        if (c < '0' || c > '9' ||
            values[field] > (8192u - (uint32_t)(c - '0')) / 10u) {
            return 0;
        }
        values[field] = values[field] * 10u + (uint32_t)(c - '0');
    }

    if (field != 2 || values[2] != 32 ||
        values[0] < CURIE_WIDTH || values[0] > 8192 ||
        values[1] < CURIE_HEIGHT || values[1] > 4320) {
        return 0;
    }
    *width = values[0];
    *height = values[1];
    return 1;
}

static int find_vbe_mode(uint32_t width,
                         uint32_t height,
                         uint16_t *mode,
                         vbe_mode_info *selected)
{
    if (!vbe_control_info ||
        vbe_control_info > 0xA0000u - VBE_CONTROL_INFO_SIZE) {
        return 0;
    }
    const volatile uint8_t *control =
        (const volatile uint8_t *)vbe_control_info;
    if (control[0] != 'V' || control[1] != 'E' ||
        control[2] != 'S' || control[3] != 'A') {
        return 0;
    }

    uint16_t list_offset = read_u16(control + 14);
    uint16_t list_segment = read_u16(control + 16);
    uint32_t mode_list =
        ((uint32_t)list_segment << 4) + list_offset;
    uint32_t code_length =
        (uint32_t)(vbe_trampoline_end - vbe_trampoline_start);
    const uint32_t mode_list_bytes = VBE_MODE_LIST_MAX * 2u;
    if (mode_list == 0 || mode_list > 0xA0000u - mode_list_bytes ||
        ranges_overlap(vbe_control_info, VBE_CONTROL_INFO_SIZE,
                       VBE_SCRATCH_ADDRESS, 64) ||
        ranges_overlap(vbe_control_info, VBE_CONTROL_INFO_SIZE,
                       VBE_MODE_INFO_ADDRESS, 256) ||
        ranges_overlap(vbe_control_info, VBE_CONTROL_INFO_SIZE,
                       VBE_TRAMPOLINE_ADDRESS, code_length) ||
        ranges_overlap(vbe_control_info, VBE_CONTROL_INFO_SIZE,
                       0x40000, 0x10000) ||
        ranges_overlap(vbe_control_info, VBE_CONTROL_INFO_SIZE,
                       mode_list, mode_list_bytes) ||
        ranges_overlap(mode_list, mode_list_bytes,
                       VBE_SCRATCH_ADDRESS, 64) ||
        ranges_overlap(mode_list, mode_list_bytes,
                       VBE_MODE_INFO_ADDRESS, 256) ||
        ranges_overlap(mode_list, mode_list_bytes,
                       VBE_TRAMPOLINE_ADDRESS, code_length) ||
        ranges_overlap(mode_list, mode_list_bytes, 0x40000, 0x10000)) {
        return 0;
    }

    const volatile uint8_t *list =
        (const volatile uint8_t *)mode_list;
    for (uint32_t i = 0; i < VBE_MODE_LIST_MAX; i++) {
        if (mode_list + i * 2u > 0xA0000u - 2u) return 0;
        uint16_t candidate = read_u16(list + i * 2u);
        if (candidate == 0xFFFFu) break;
        if (vbe_bios_call(0x4F01u, 0, candidate) != 0x004Fu) continue;

        const volatile uint8_t *mode_bytes =
            (const volatile uint8_t *)VBE_MODE_INFO_ADDRESS;
        volatile const vbe_mode_info *info =
            (volatile const vbe_mode_info *)mode_bytes;
        if ((info->attributes & 0x0091u) != 0x0091u ||
            info->x_resolution != width ||
            info->y_resolution != height ||
            info->bits_per_pixel != 32 ||
            info->memory_model != 6 ||
            info->bytes_per_scan_line < width * 4u) {
            continue;
        }

        *mode = candidate;
        uint8_t *destination = (uint8_t *)selected;
        for (size_t j = 0; j < sizeof(*selected); j++) {
            destination[j] = mode_bytes[j];
        }
        return 1;
    }
    return 0;
}

static int map_vbe_framebuffer(uint32_t address, uint32_t length)
{
    uint32_t start = address & ~0xFFFu;
    uint32_t end = (address + length + 0xFFFu) & ~0xFFFu;
    const uint32_t identity_limit = 0x400000u;
    if (start < identity_limit) start = identity_limit;
    if (start >= end) return 1;

    lucid_status status = vmm_map_kernel_range(
        start, start, end - start, VMM_KERNEL | VMM_WRITABLE
    );
    return status.ok == 0;
}

static void apply_resolution_config(void)
{
    static const char path[] = "/system/config/resolution";
    char config[32];
    size_t length = 0;
    if (ramfs_read(path, config, sizeof(config), &length) != 0) {
        terminal_set_status("Display resolution config unreadable");
        return;
    }

    uint32_t width;
    uint32_t height;
    if (!parse_resolution(config, length, &width, &height)) {
        terminal_set_status("Invalid display resolution");
        return;
    }
    if (framebuffer && framebuffer_width == width &&
        framebuffer_height == height) {
        return;
    }
    if (!framebuffer) {
        terminal_set_status("VBE framebuffer unavailable");
        return;
    }

    uint16_t mode = 0;
    vbe_mode_info info;
    if (!find_vbe_mode(width, height, &mode, &info)) {
        terminal_set_status("VBE resolution is unavailable");
        return;
    }

    uint64_t length64 =
        (uint64_t)info.bytes_per_scan_line * info.y_resolution;
    uint64_t end = (uint64_t)info.physical_base + length64;
    if (info.physical_base == 0 ||
        length64 == 0 || length64 > 64u * 1024u * 1024u ||
        end > 0xFFFFF000ULL ||
        !map_vbe_framebuffer(info.physical_base, (uint32_t)length64)) {
        terminal_set_status("VBE framebuffer mapping failed");
        return;
    }

    if (vbe_bios_call(0x4F02u, (uint16_t)(mode | 0x4000u), 0) != 0x004Fu) {
        terminal_set_status("VBE mode change failed");
        return;
    }

    framebuffer_phys = info.physical_base;
    framebuffer_length = (uint32_t)length64;
    framebuffer_pitch = info.bytes_per_scan_line;
    framebuffer_width = info.x_resolution;
    framebuffer_height = info.y_resolution;
    framebuffer = (volatile uint8_t *)framebuffer_phys;
    framebuffer_foreground = 0xFFFFFFFFu;
    framebuffer_background = 0;
    framebuffer_status = "Runtime VBE framebuffer active";
    terminal_set_status("Display resolution changed");
}
static int terminal_cursor_visible;

static void framebuffer_pixel(uint32_t x, uint32_t y, uint32_t color)
{
    volatile uint32_t *row =
        (volatile uint32_t *)(framebuffer + y * framebuffer_pitch);
    row[x] = color;
}

static void framebuffer_clear(void)
{
    for (uint32_t y = 0; y < framebuffer_height; y++) {
        for (uint32_t x = 0; x < framebuffer_width; x++) {
            framebuffer_pixel(x, y, framebuffer_background);
        }
    }
}

static void framebuffer_draw_glyph_colors(size_t row,
                                          size_t col,
                                          uint8_t glyph,
                                          uint32_t foreground,
                                          uint32_t background)
{
    if (glyph >= 128) glyph = '?';

    uint32_t scale = terminal_zoom;
    for (uint32_t y = 0; y < CURIE_HEIGHT; y++) {
        uint8_t bits = curie_font[glyph][y];
        for (uint32_t y_scale = 0; y_scale < scale; y_scale++) {
            for (uint32_t x = 0; x < CURIE_WIDTH; x++) {
                uint32_t color = (bits & (1u << (CURIE_WIDTH - 1 - x)))
                    ? foreground
                    : background;
                for (uint32_t x_scale = 0; x_scale < scale; x_scale++) {
                    framebuffer_pixel(
                        (uint32_t)(col * CURIE_WIDTH * scale +
                                   x * scale + x_scale),
                        (uint32_t)(row * CURIE_HEIGHT * scale +
                                   y * scale + y_scale),
                        color
                    );
                }
            }
        }
    }
}

static int terminal_surfaces_initialized;

static uint32_t screen_columns(void)
{
    uint32_t columns = framebuffer
        ? framebuffer_width / (CURIE_WIDTH * terminal_zoom)
        : VGA_WIDTH;
    if (columns > TERMINAL_MAX_COLUMNS) columns = TERMINAL_MAX_COLUMNS;
    return columns;
}

static uint32_t screen_rows(void)
{
    uint32_t rows = framebuffer
        ? framebuffer_height / (CURIE_HEIGHT * terminal_zoom)
        : VGA_HEIGHT;
    if (rows > TERMINAL_MAX_ROWS) rows = TERMINAL_MAX_ROWS;
    return rows;
}

static uint32_t terminal_surface_slot(void)
{
    uint32_t slot = usermode_process_current_slot();
    return slot == 0 ? 1 : slot;
}

static void update_surface_dimensions(void)
{
    uint32_t columns = screen_columns();
    uint32_t rows = screen_rows();
    for (uint32_t slot = 0; slot < USERMODE_PROCESS_SLOTS; slot++) {
        terminal_surface *surface = &terminal_surfaces[slot];
        surface->columns = columns;
        surface->rows = rows;
        if (surface->row >= rows) surface->row = rows - 1;
        if (surface->column >= columns) surface->column = columns - 1;
    }
}

static void surface_clear(terminal_surface *surface)
{
    for (uint32_t row = 0; row < TERMINAL_MAX_ROWS; row++) {
        for (uint32_t column = 0; column < TERMINAL_MAX_COLUMNS; column++) {
            surface->cells[row][column] = ' ';
        }
    }
    surface->row = 0;
    surface->column = 0;
    surface->csi_parameter = 0;
    surface->csi_second_parameter = 0;
    surface->csi_second_parameter_active = 0;
    surface->lambda_prefix = 0;
    surface->escape_state = 0;
    surface->wrap_pending = 0;
}

static void initialize_surfaces(void)
{
    if (terminal_surfaces_initialized) return;
    terminal_surfaces_initialized = 1;
    uint32_t columns = screen_columns();
    uint32_t rows = screen_rows();
    for (uint32_t slot = 0; slot < USERMODE_PROCESS_SLOTS; slot++) {
        surface_clear(&terminal_surfaces[slot]);
        terminal_surfaces[slot].columns = columns;
        terminal_surfaces[slot].rows = rows;
    }
    terminal_windows[1] =
        (terminal_window){ 0, 0, columns, rows, 1, 1 };
}

static void draw_screen_cell(uint32_t row,
                             uint32_t column,
                             char character,
                             int highlighted);

static int terminal_config_value(const char *config,
                                 size_t length,
                                 const char *name,
                                 const char **value,
                                 size_t *value_length)
{
    size_t name_length = 0;
    while (name[name_length]) name_length++;

    size_t offset = 0;
    while (offset < length) {
        size_t end = offset;
        while (end < length && config[end] != '\n' &&
               config[end] != '\r') {
            end++;
        }
        size_t start = offset;
        while (start < end &&
               (config[start] == ' ' || config[start] == '\t')) {
            start++;
        }
        if (start < end && config[start] != '#') {
            size_t equals = start;
            while (equals < end && config[equals] != '=') equals++;
            size_t key_end = equals;
            while (key_end > start &&
                   (config[key_end - 1] == ' ' ||
                    config[key_end - 1] == '\t')) {
                key_end--;
            }
            if (equals < end && key_end - start == name_length) {
                size_t i = 0;
                while (i < name_length &&
                       config[start + i] == name[i]) {
                    i++;
                }
                if (i == name_length) {
                    start = equals + 1;
                    while (start < end &&
                           (config[start] == ' ' ||
                            config[start] == '\t')) {
                        start++;
                    }
                    while (end > start &&
                           (config[end - 1] == ' ' ||
                            config[end - 1] == '\t')) {
                        end--;
                    }
                    *value = config + start;
                    *value_length = end - start;
                    return 1;
                }
            }
        }
        offset = end + 1;
    }
    return 0;
}

static int config_value_equals(const char *value,
                               size_t value_length,
                               const char *expected)
{
    size_t expected_length = 0;
    while (expected[expected_length]) expected_length++;
    if (value_length != expected_length) return 0;
    for (size_t i = 0; i < value_length; i++) {
        if (value[i] != expected[i]) return 0;
    }
    return 1;
}

static int parse_terminal_color(const char *value,
                               size_t length,
                               uint32_t *color)
{
    if (length != 6) return 0;
    uint32_t result = 0;
    for (size_t i = 0; i < length; i++) {
        char digit = value[i];
        uint32_t nibble;
        if (digit >= '0' && digit <= '9') {
            nibble = (uint32_t)(digit - '0');
        } else if (digit >= 'a' && digit <= 'f') {
            nibble = (uint32_t)(digit - 'a') + 10u;
        } else if (digit >= 'A' && digit <= 'F') {
            nibble = (uint32_t)(digit - 'A') + 10u;
        } else {
            return 0;
        }
        result = (result << 4) | nibble;
    }
    *color = 0xFF000000u | result;
    return 1;
}

static void load_terminal_config(void)
{
    if (terminal_config_loaded) return;
    terminal_config_loaded = 1;
    terminal_bar = TERMINAL_BAR_TOP;
    framebuffer_foreground = 0xFFFFFFFFu;
    framebuffer_background = 0xFF000000u;
    framebuffer_accent = 0xFF00FF00u;
    terminal_zoom = 1;

    static const char zoom_path[] = "/system/config/clearsh/zoom";
    char zoom_config[8];
    size_t zoom_length = 0;
    if (ramfs_read(zoom_path, zoom_config, sizeof(zoom_config),
                   &zoom_length) == 0) {
        while (zoom_length &&
               (zoom_config[zoom_length - 1] == '\n' ||
                zoom_config[zoom_length - 1] == '\r')) {
            zoom_length--;
        }
        if (zoom_length == 1 && zoom_config[0] >= '1' &&
            zoom_config[0] <= '0' + TERMINAL_ZOOM_MAX) {
            terminal_zoom = (uint32_t)(zoom_config[0] - '0');
        } else {
            static const char invalid[] = "Invalid Clearsh zoom (1-4)";
            terminal_set_status(invalid);
        }
    }

    apply_resolution_config();

    static const char theme_path[] = "/system/config/brain/theme";
    static const char legacy_path[] = "/system/config/brain.conf";
    char theme[512];
    char legacy[128];
    size_t theme_length = 0;
    size_t legacy_length = 0;
    if (ramfs_read(theme_path, theme, sizeof(theme), &theme_length) != 0 ||
        theme_length >= sizeof(theme)) {
        terminal_set_status("Brain theme config unreadable or too large");
        return;
    }
    if (ramfs_read(legacy_path, legacy, sizeof(legacy), &legacy_length) != 0 ||
        legacy_length >= sizeof(legacy)) {
        legacy_length = 0;
    }

    const char *value;
    size_t value_length;
    int has_bar = terminal_config_value(
        theme, theme_length, "bar", &value, &value_length
    );
    if (!has_bar) {
        has_bar = terminal_config_value(
            legacy, legacy_length, "bar", &value, &value_length
        );
    }
    if (has_bar) {
        if (config_value_equals(value, value_length, "top")) {
            terminal_bar = TERMINAL_BAR_TOP;
        } else if (config_value_equals(value, value_length, "bottom")) {
            terminal_bar = TERMINAL_BAR_BOTTOM;
        } else if (config_value_equals(value, value_length, "left")) {
            terminal_bar = TERMINAL_BAR_LEFT;
        } else if (config_value_equals(value, value_length, "right")) {
            terminal_bar = TERMINAL_BAR_RIGHT;
        } else {
            terminal_set_status("Invalid bar position");
        }
    }

    uint32_t color;
    if (terminal_config_value(theme, theme_length, "foreground",
                              &value, &value_length)) {
        if (parse_terminal_color(value, value_length, &color)) {
            framebuffer_foreground = color;
        } else {
            terminal_set_status("Invalid Brain foreground color");
        }
    }
    if (terminal_config_value(theme, theme_length, "background",
                              &value, &value_length)) {
        if (parse_terminal_color(value, value_length, &color)) {
            framebuffer_background = color;
        } else {
            terminal_set_status("Invalid Brain background color");
        }
    }
    if (terminal_config_value(theme, theme_length, "accent",
                              &value, &value_length)) {
        if (parse_terminal_color(value, value_length, &color)) {
            framebuffer_accent = color;
        } else {
            terminal_set_status("Invalid Brain accent color");
        }
    }
}

static void append_status_text(char *buffer,
                               uint32_t capacity,
                               uint32_t *length,
                               const char *text)
{
    while (*text && *length < capacity) {
        buffer[(*length)++] = *text++;
    }
}

static void append_status_number(char *buffer,
                                 uint32_t capacity,
                                 uint32_t *length,
                                 uint32_t number)
{
    char digits[10];
    uint32_t count = 0;
    do {
        digits[count++] = (char)('0' + number % 10);
        number /= 10;
    } while (number && count < sizeof(digits));
    while (count && *length < capacity) {
        buffer[(*length)++] = digits[--count];
    }
}

static void append_script_status(char *buffer,
                                 uint32_t capacity,
                                 uint32_t *length)
{
    static const char config_path[] = "/system/config/brain/statusbar";
    char config[512];
    size_t config_length = 0;
    if (ramfs_read(config_path, config, sizeof(config), &config_length) != 0 ||
        config_length >= sizeof(config)) {
        return;
    }

    const char *value;
    size_t value_length;
    if (terminal_config_value(config, config_length, "enabled",
                              &value, &value_length) &&
        !config_value_equals(value, value_length, "1")) {
        return;
    }
    if (!terminal_config_value(config, config_length, "output",
                               &value, &value_length) ||
        value_length == 0 ||
        value_length >= TERMINAL_STATUSBAR_PATH_MAX ||
        value[0] != '/') {
        return;
    }

    char output_path[TERMINAL_STATUSBAR_PATH_MAX];
    for (size_t i = 0; i < value_length; i++) {
        if (value[i] <= ' ' || value[i] == 0x7F) return;
        output_path[i] = value[i];
    }
    output_path[value_length] = '\0';

    char output[TERMINAL_STATUSBAR_TEXT_MAX];
    size_t output_length = 0;
    if (ramfs_read(output_path, output, sizeof(output), &output_length) != 0) {
        return;
    }
    if (output_length > sizeof(output)) return;
    while (output_length &&
           (output[output_length - 1] == '\n' ||
            output[output_length - 1] == '\r')) {
        output_length--;
    }
    if (!output_length) return;

    append_status_text(buffer, capacity, length, " | ");
    for (size_t i = 0; i < output_length && *length < capacity; i++) {
        buffer[(*length)++] = output[i];
    }
}

static uint8_t vga_color_index(uint32_t color)
{
    static const uint32_t palette[16] = {
        0x000000u, 0x0000AAu, 0x00AA00u, 0x00AAAAu,
        0xAA0000u, 0xAA00AAu, 0xAA5500u, 0xAAAAAAu,
        0x555555u, 0x5555FFu, 0x55FF55u, 0x55FFFFu,
        0xFF5555u, 0xFF55FFu, 0xFFFF55u, 0xFFFFFFu
    };
    uint32_t red = (color >> 16) & 0xFFu;
    uint32_t green = (color >> 8) & 0xFFu;
    uint32_t blue = color & 0xFFu;
    uint32_t best_distance = 0xFFFFFFFFu;
    uint8_t best_index = 0;
    for (uint8_t i = 0; i < 16; i++) {
        uint32_t palette_red = (palette[i] >> 16) & 0xFFu;
        uint32_t palette_green = (palette[i] >> 8) & 0xFFu;
        uint32_t palette_blue = palette[i] & 0xFFu;
        int32_t red_delta = (int32_t)red - (int32_t)palette_red;
        int32_t green_delta = (int32_t)green - (int32_t)palette_green;
        int32_t blue_delta = (int32_t)blue - (int32_t)palette_blue;
        uint32_t distance = (uint32_t)(
            red_delta * red_delta +
            green_delta * green_delta +
            blue_delta * blue_delta
        );
        if (distance < best_distance) {
            best_distance = distance;
            best_index = i;
        }
    }
    return best_index;
}

static uint8_t vga_attribute(uint32_t foreground, uint32_t background)
{
    uint8_t foreground_index = vga_color_index(foreground);
    uint8_t background_index = vga_color_index(background) & 0x07u;
    return (uint8_t)((background_index << 4) | foreground_index);
}

static void draw_status_bar(void)
{
    uint32_t columns = screen_columns();
    uint32_t rows = screen_rows();
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t width = columns;
    uint32_t height = 1;
    if (terminal_bar == TERMINAL_BAR_BOTTOM) {
        y = rows - 1;
    } else if (terminal_bar == TERMINAL_BAR_LEFT ||
               terminal_bar == TERMINAL_BAR_RIGHT) {
        width = TERMINAL_BAR_WIDTH;
        if (width >= columns) width = columns > 1 ? columns - 1 : 1;
        height = rows;
        if (terminal_bar == TERMINAL_BAR_RIGHT) x = columns - width;
    } else if (terminal_bar == TERMINAL_BAR_TOP) {
        y = 0;
    }

    for (uint32_t row = 0; row < height; row++) {
        for (uint32_t column = 0; column < width; column++) {
            draw_screen_cell(y + row, x + column, ' ', 1);
        }
    }

    char text[160];
    uint32_t length = 0;
    uint32_t window_count =
        usermode_process_current_desktop() ==
            usermode_process_log_desktop() ? 1 : 0;
    for (uint32_t slot = 2; slot < USERMODE_PROCESS_SLOTS; slot++) {
        if (usermode_process_window_visible(slot)) window_count++;
    }
    append_status_text(text, sizeof(text), &length, "Brain | desk ");
    append_status_number(text, sizeof(text), &length,
                         usermode_process_current_desktop());
    append_status_text(text, sizeof(text), &length, "/");
    append_status_number(text, sizeof(text), &length,
                         usermode_process_desktop_count());
    append_status_text(text, sizeof(text), &length, " | windows ");
    append_status_number(text, sizeof(text), &length, window_count);
    append_status_text(text, sizeof(text), &length, " | focus ");
    append_status_number(text, sizeof(text), &length,
                         usermode_process_window_pid(
                             usermode_process_focused_slot()
                         ));
    append_status_text(text, sizeof(text), &length, " | ");
    append_status_text(text, sizeof(text), &length, terminal_status);
    append_script_status(text, sizeof(text), &length);

    uint32_t capacity = terminal_bar == TERMINAL_BAR_LEFT ||
                        terminal_bar == TERMINAL_BAR_RIGHT
        ? width * height
        : width;
    if (length > capacity) length = capacity;
    for (uint32_t i = 0; i < length; i++) {
        uint32_t draw_x = x + (terminal_bar == TERMINAL_BAR_LEFT ||
                               terminal_bar == TERMINAL_BAR_RIGHT
            ? i % width
            : i);
        uint32_t draw_y = y + (terminal_bar == TERMINAL_BAR_LEFT ||
                               terminal_bar == TERMINAL_BAR_RIGHT
            ? i / width
            : 0);
        draw_screen_cell(draw_y, draw_x, text[i], 1);
    }
}

static void draw_screen_cell(uint32_t row,
                             uint32_t column,
                             char character,
                             int highlighted)
{
    if (row >= screen_rows() || column >= screen_columns()) return;
    if (framebuffer) {
        uint32_t foreground = highlighted
            ? framebuffer_accent
            : framebuffer_foreground;
        framebuffer_draw_glyph_colors(
            row, column, (uint8_t)character, foreground,
            framebuffer_background
        );
    } else {
        uint8_t attribute = vga_attribute(
            highlighted ? framebuffer_accent : framebuffer_foreground,
            framebuffer_background
        );
        vga_buf[row * VGA_WIDTH + column] =
            ((uint16_t)attribute << 8) | (uint8_t)character;
    }
}

static void draw_cursor(uint32_t slot, int visible)
{
    if (slot >= USERMODE_PROCESS_SLOTS || !terminal_windows[slot].visible) {
        return;
    }
    terminal_surface *surface = &terminal_surfaces[slot];
    terminal_window *window = &terminal_windows[slot];
    uint32_t title_rows = window->title ? 1 : 0;
    if (surface->row >= window->height - title_rows ||
        surface->column >= window->width) {
        return;
    }
    char character = surface->cells[surface->row][surface->column];
    uint32_t x = window->x + surface->column;
    uint32_t y = window->y + title_rows + surface->row;
    draw_screen_cell(y, x, character, visible);
}

static void draw_surface_window(uint32_t slot)
{
    if (slot >= USERMODE_PROCESS_SLOTS || !terminal_windows[slot].visible) {
        return;
    }
    terminal_surface *surface = &terminal_surfaces[slot];
    terminal_window *window = &terminal_windows[slot];
    uint32_t title_rows = window->title ? 1 : 0;
    if (window->title) {
        for (uint32_t x = 0; x < window->width; x++) {
            draw_screen_cell(window->y, window->x + x, ' ',
                             slot == 1 ||
                             usermode_process_window_focused(slot));
        }
        uint32_t offset = 0;
        if (slot == 1) {
            static const char title[] = "[Logs]";
            while (title[offset] && offset < window->width) {
                draw_screen_cell(window->y, window->x + offset,
                                 title[offset], 1);
                offset++;
            }
        } else {
            static const char title[] = "[Eye ";
            while (title[offset] && offset < window->width) {
                draw_screen_cell(window->y, window->x + offset, title[offset],
                                 usermode_process_window_focused(slot));
                offset++;
            }
            uint32_t pid = usermode_process_window_pid(slot);
            char digits[10];
            uint32_t count = 0;
            do {
                digits[count++] = (char)('0' + pid % 10);
                pid /= 10;
            } while (pid && count < sizeof(digits));
            while (count && offset < window->width) {
                draw_screen_cell(window->y, window->x + offset,
                                 digits[--count],
                                 usermode_process_window_focused(slot));
                offset++;
            }
            if (offset < window->width) {
                draw_screen_cell(window->y, window->x + offset++, ']',
                                 usermode_process_window_focused(slot));
            }
            if (usermode_process_window_focused(slot) &&
                offset + 8 < window->width) {
                static const char focused[] = " focused";
                for (uint32_t i = 0; focused[i]; i++) {
                    draw_screen_cell(window->y, window->x + offset + i,
                                     focused[i], 1);
                }
            }
        }
    }

    uint32_t content_rows = window->height - title_rows;
    for (uint32_t row = 0; row < content_rows; row++) {
        for (uint32_t column = 0; column < window->width; column++) {
            draw_screen_cell(window->y + title_rows + row,
                             window->x + column,
                             surface->cells[row][column], 0);
        }
    }
    if (terminal_cursor_visible &&
        usermode_process_window_focused(slot)) {
        draw_cursor(slot, 1);
    }
}

static void refresh_window_layout(void)
{
    load_terminal_config();
    uint32_t columns = screen_columns();
    uint32_t rows = screen_rows();
    uint32_t origin_x = 0;
    uint32_t origin_y = 0;
    uint32_t workspace_width = columns;
    uint32_t workspace_height = rows;
    if (terminal_bar == TERMINAL_BAR_TOP) {
        origin_y = rows > 1 ? 1 : 0;
        workspace_height = rows > 1 ? rows - 1 : rows;
    } else if (terminal_bar == TERMINAL_BAR_BOTTOM) {
        workspace_height = rows > 1 ? rows - 1 : rows;
    } else {
        uint32_t bar_width = TERMINAL_BAR_WIDTH;
        if (bar_width >= columns) bar_width = columns > 1 ? columns - 1 : 0;
        workspace_width = columns - bar_width;
        if (terminal_bar == TERMINAL_BAR_LEFT) origin_x = bar_width;
    }

    uint32_t visible[USERMODE_PROCESS_SLOTS];
    uint32_t count = 0;
    if (usermode_process_current_desktop() ==
        usermode_process_log_desktop()) {
        visible[count++] = 1;
    }
    for (uint32_t slot = 2; slot < USERMODE_PROCESS_SLOTS; slot++) {
        if (usermode_process_window_visible(slot)) {
            visible[count++] = slot;
        }
    }

    for (uint32_t slot = 0; slot < USERMODE_PROCESS_SLOTS; slot++) {
        terminal_windows[slot].visible = 0;
    }
    if (count == 0) {
        return;
    } else if (count == 1) {
        uint32_t slot = visible[0];
        terminal_windows[slot] =
            (terminal_window){
                origin_x, origin_y, workspace_width, workspace_height,
                slot == 1, 1
            };
    } else if (workspace_width < 24) {
        uint32_t y = origin_y;
        for (uint32_t i = 0; i < count; i++) {
            uint32_t remaining = origin_y + workspace_height - y;
            uint32_t height = remaining / (count - i);
            if (height == 0) height = 1;
            terminal_windows[visible[i]] = (terminal_window){
                origin_x, y, workspace_width, height, 1, 1
            };
            y += height;
        }
    } else {
        uint32_t master_width =
            workspace_width * usermode_process_master_ratio() / 100;
        if (master_width < 12) master_width = 12;
        if (master_width > workspace_width - 12) {
            master_width = workspace_width - 12;
        }
        terminal_windows[visible[0]] =
            (terminal_window){
                origin_x, origin_y, master_width, workspace_height, 1, 1
            };

        uint32_t stack_width = workspace_width - master_width;
        uint32_t y = origin_y;
        for (uint32_t i = 1; i < count; i++) {
            uint32_t remaining = origin_y + workspace_height - y;
            uint32_t height = remaining / (count - i);
            if (height == 0) height = 1;
            terminal_windows[visible[i]] = (terminal_window){
                origin_x + master_width, y, stack_width, height, 1, 1
            };
            y += height;
        }
    }

    for (uint32_t slot = 0; slot < USERMODE_PROCESS_SLOTS; slot++) {
        if (!terminal_windows[slot].visible) continue;
        terminal_surface *surface = &terminal_surfaces[slot];
        uint32_t title_rows = terminal_windows[slot].title ? 1 : 0;
        surface->columns = terminal_windows[slot].width;
        surface->rows = terminal_windows[slot].height - title_rows;
        if (surface->columns > TERMINAL_MAX_COLUMNS) {
            surface->columns = TERMINAL_MAX_COLUMNS;
        }
        if (surface->rows > TERMINAL_MAX_ROWS) {
            surface->rows = TERMINAL_MAX_ROWS;
        }
        if (surface->columns == 0) surface->columns = 1;
        if (surface->rows == 0) surface->rows = 1;
        if (surface->row >= surface->rows) {
            surface->row = surface->rows - 1;
        }
        if (surface->column >= surface->columns) {
            surface->column = surface->columns - 1;
        }
    }
}

void terminal_refresh(void)
{
    initialize_surfaces();
    update_surface_dimensions();
    refresh_window_layout();
    if (framebuffer) {
        framebuffer_clear();
    } else {
        uint8_t attribute = vga_attribute(framebuffer_foreground,
                                          framebuffer_background);
        for (uint32_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
            vga_buf[i] = ((uint16_t)attribute << 8) | ' ';
        }
    }
    for (uint32_t slot = 0; slot < USERMODE_PROCESS_SLOTS; slot++) {
        draw_surface_window(slot);
    }
    draw_status_bar();
}

uint32_t terminal_get_size(void)
{
    initialize_surfaces();
    uint32_t slot = terminal_surface_slot();
    terminal_surface *surface = &terminal_surfaces[slot];
    return (surface->columns << 16) | surface->rows;
}

void terminal_config_reload(void)
{
    terminal_config_loaded = 0;
    terminal_refresh();
}

void terminal_statusbar_refresh(void)
{
    if (!terminal_config_loaded) load_terminal_config();
    initialize_surfaces();
    update_surface_dimensions();
    draw_status_bar();
}

static void redraw_surface(uint32_t slot)
{
    if (slot >= USERMODE_PROCESS_SLOTS) return;
    if (!terminal_windows[slot].visible) return;
    draw_surface_window(slot);
}

static void redraw_surface_row(uint32_t slot, uint32_t row)
{
    if (slot >= USERMODE_PROCESS_SLOTS || !terminal_windows[slot].visible) {
        return;
    }

    terminal_surface *surface = &terminal_surfaces[slot];
    terminal_window *window = &terminal_windows[slot];
    uint32_t title_rows = window->title ? 1 : 0;
    if (row >= surface->rows) return;
    for (uint32_t column = 0; column < surface->columns; column++) {
        draw_screen_cell(window->y + title_rows + row,
                         window->x + column,
                         surface->cells[row][column], 0);
    }
    if (terminal_cursor_visible &&
        usermode_process_window_focused(slot)) {
        draw_cursor(slot, 1);
    }
}

void terminal_process_reset(uint32_t slot)
{
    if (slot >= USERMODE_PROCESS_SLOTS) return;
    initialize_surfaces();
    surface_clear(&terminal_surfaces[slot]);
    terminal_surfaces[slot].columns = screen_columns();
    terminal_surfaces[slot].rows = screen_rows();
    terminal_refresh();
}

void terminal_set_status(const char *text)
{
    uint32_t length = 0;
    if (!text) text = "";
    while (text[length] && length + 1 < sizeof(terminal_status)) {
        terminal_status[length] = text[length];
        length++;
    }
    terminal_status[length] = '\0';
}

static void surface_scroll(terminal_surface *surface)
{
    for (uint32_t row = 1; row < surface->rows; row++) {
        for (uint32_t column = 0; column < surface->columns; column++) {
            surface->cells[row - 1][column] =
                surface->cells[row][column];
        }
    }
    for (uint32_t column = 0; column < surface->columns; column++) {
        surface->cells[surface->rows - 1][column] = ' ';
    }
    surface->row = surface->rows - 1;
    surface->column = 0;
    surface->wrap_pending = 0;
}

static void surface_clear_to_end(terminal_surface *surface, int whole_screen)
{
    uint32_t last_row = whole_screen ? surface->rows - 1 : surface->row;
    for (uint32_t row = surface->row; row <= last_row; row++) {
        uint32_t start = row == surface->row ? surface->column : 0;
        for (uint32_t column = start; column < surface->columns; column++) {
            surface->cells[row][column] = ' ';
        }
    }
}

static void surface_csi_execute(uint32_t slot, uint8_t final)
{
    terminal_surface *surface = &terminal_surfaces[slot];
    if (final == 'H' || final == 'f') {
        surface->row = surface->csi_parameter
            ? surface->csi_parameter - 1
            : 0;
        if (surface->row >= surface->rows) {
            surface->row = surface->rows - 1;
        }
        surface->column = surface->csi_second_parameter_active &&
                          surface->csi_second_parameter
            ? surface->csi_second_parameter - 1
            : 0;
        if (surface->column >= surface->columns) {
            surface->column = surface->columns - 1;
        }
        surface->wrap_pending = 0;
    } else if (final == 'J') {
        if (surface->csi_parameter == 2) {
            surface_clear(surface);
        } else {
            surface_clear_to_end(surface, 1);
        }
        redraw_surface(slot);
    } else if (final == 'K') {
        uint32_t row = surface->row;
        surface_clear_to_end(surface, 0);
        redraw_surface_row(slot, row);
    }
}

static void surface_put_ascii(uint32_t slot, char character)
{
    terminal_surface *surface = &terminal_surfaces[slot];
    int redraw = 0;
    if (character == '\r') {
        surface->column = 0;
        surface->wrap_pending = 0;
    } else if (character == '\n') {
        surface->column = 0;
        surface->wrap_pending = 0;
        surface->row++;
        if (surface->row >= surface->rows) {
            surface_scroll(surface);
            redraw = 1;
        }
    } else if (character == '\b') {
        if (surface->wrap_pending) {
            surface->wrap_pending = 0;
        } else if (surface->column > 0) {
            surface->column--;
        }
    } else if ((uint8_t)character >= 32) {
        if (surface->wrap_pending ||
            surface->column >= surface->columns) {
            surface->column = 0;
            surface->row++;
            surface->wrap_pending = 0;
        }
        if (surface->row >= surface->rows) {
            surface_scroll(surface);
            redraw = 1;
        }
        uint32_t row = surface->row;
        uint32_t column = surface->column;
        surface->cells[row][column] = character;
        if (surface->column + 1 >= surface->columns) {
            surface->wrap_pending = 1;
        } else {
            surface->column++;
        }
        if (!redraw && terminal_windows[slot].visible) {
            terminal_window *window = &terminal_windows[slot];
            uint32_t title_rows = window->title ? 1 : 0;
            draw_screen_cell(window->y + title_rows + row,
                             window->x + column, character, 0);
        }
    }

    if (redraw) {
        redraw_surface(slot);
    } else if (terminal_cursor_visible &&
               usermode_process_window_focused(slot)) {
        draw_cursor(slot, 1);
    }
}

void terminal_clear(void)
{
    uint32_t slot = terminal_surface_slot();
    initialize_surfaces();
    surface_clear(&terminal_surfaces[slot]);
    redraw_surface(slot);
    terminal_io_clear();
}

void terminal_cursor_show(void)
{
    if (terminal_cursor_visible) return;
    terminal_cursor_visible = 1;
    draw_cursor(terminal_surface_slot(), 1);
}

void terminal_cursor_hide(void)
{
    if (!terminal_cursor_visible) return;
    draw_cursor(terminal_surface_slot(), 0);
    terminal_cursor_visible = 0;
}

void terminal_putc(char c)
{
    uint32_t slot = terminal_surface_slot();
    initialize_surfaces();
    terminal_surface *surface = &terminal_surfaces[slot];
    uint8_t byte = (uint8_t)c;

    if (surface->escape_state == 1) {
        surface->escape_state = 0;
        if (byte == '[') {
            surface->escape_state = 2;
            surface->csi_parameter = 0;
            surface->csi_second_parameter = 0;
            surface->csi_second_parameter_active = 0;
        }
    } else if (surface->escape_state == 2) {
        if (byte >= '0' && byte <= '9') {
            uint32_t digit = (uint32_t)(byte - '0');
            uint32_t *parameter = surface->csi_second_parameter_active
                ? &surface->csi_second_parameter
                : &surface->csi_parameter;
            if (*parameter <= (999u - digit) / 10u) {
                *parameter = *parameter * 10u + digit;
            }
        } else if (byte == ';' &&
                   !surface->csi_second_parameter_active) {
            surface->csi_second_parameter_active = 1;
        } else {
            surface->escape_state = 0;
            if (byte >= 0x40 && byte <= 0x7E) {
                surface_csi_execute(slot, byte);
                if (terminal_cursor_visible &&
                    usermode_process_window_focused(slot)) {
                    draw_cursor(slot, 1);
                }
            }
        }
    } else if (byte == 0x1B) {
        surface->escape_state = 1;
    } else if (surface->lambda_prefix) {
        surface->lambda_prefix = 0;
        surface_put_ascii(slot, byte == 0xBB ? 'l' : '?');
    } else if (byte == 0xCE) {
        surface->lambda_prefix = 1;
    } else if (byte >= 0x80) {
        surface_put_ascii(slot, '?');
    } else {
        surface_put_ascii(slot, c);
    }

    if (c == '\n') terminal_io_putc('\r');
    terminal_io_putc(c);
}

void terminal_write(const char *s)
{
    while (*s) terminal_putc(*s++);
}

void terminal_flush(void)
{
    terminal_io_flush();
}

lucid_status terminal_pre(void)
{
    initialize_surfaces();
    return LUCID_OK_MSG("Preparing video and input consoles");
}

void terminal_set_boot_info(const multiboot_info *info)
{
    if (!info) return;
    if (info->flags & MULTIBOOT_INFO_VBE) {
        vbe_control_info = info->vbe_control_info;
    }
    if (!(info->flags & MULTIBOOT_INFO_FRAMEBUFFER)) return;

    if (info->framebuffer_type != MULTIBOOT_FRAMEBUFFER_RGB ||
        info->framebuffer_bpp != 32) {
        framebuffer_status =
            "GRUB framebuffer is not 32-bit RGB; using VGA output";
        return;
    }
    if (info->framebuffer_addr > 0xFFFFFFFFULL) {
        framebuffer_status =
            "GRUB framebuffer is above 4 GiB; using VGA output";
        return;
    }
    if (info->framebuffer_width < CURIE_WIDTH ||
        info->framebuffer_height < CURIE_HEIGHT ||
        info->framebuffer_width > 8192 ||
        info->framebuffer_height > 4320) {
        framebuffer_status =
            "GRUB framebuffer dimensions unsupported; using VGA output";
        return;
    }
    if (info->framebuffer_pitch < info->framebuffer_width * 4u ||
        (info->framebuffer_pitch & 3u) != 0) {
        framebuffer_status =
            "GRUB framebuffer pitch unsupported; using VGA output";
        return;
    }
    uint64_t length =
        (uint64_t)info->framebuffer_pitch * info->framebuffer_height;
    uint64_t end = info->framebuffer_addr + length;
    if (length == 0 || length > 64u * 1024u * 1024u ||
        end > 0xFFFFF000ULL) {
        framebuffer_status =
            "GRUB framebuffer memory range unsupported; using VGA output";
        return;
    }

    framebuffer_phys = (uint32_t)info->framebuffer_addr;
    framebuffer = (volatile uint8_t *)framebuffer_phys;
    framebuffer_length = (uint32_t)length;
    framebuffer_pitch = info->framebuffer_pitch;
    framebuffer_width = info->framebuffer_width;
    framebuffer_height = info->framebuffer_height;
    /* All color bits set produces white for a 32-bit RGB framebuffer. */
    framebuffer_foreground = 0xFFFFFFFFu;
    framebuffer_background = 0;
    framebuffer_status = "Curie framebuffer output ready";
}

int terminal_framebuffer_mapping(uint32_t *base, uint32_t *length)
{
    if (!framebuffer || !base || !length) {
        return 0;
    }
    *base = framebuffer_phys;
    *length = framebuffer_length;
    return 1;
}

lucid_status terminal_init(void)
{
    terminal_io_init();
    if (framebuffer) {
        if (!terminal_io_available()) {
            return LUCID_OK_MSG(
                "Curie framebuffer output and PS/2 input ready"
            );
        }
        return LUCID_OK_MSG(
            "COM1, Curie framebuffer output, and serial/PS2 input ready"
        );
    }
    return LUCID_OK_MSG(framebuffer_status);
}

lucid_status terminal_post(void)
{
    return LUCID_OK;
}

int terminal_getchar(void)
{
    for (;;) {
        int queued = usermode_process_read_key();
        if (queued >= 0) return queued;

        int key = terminal_input_poll_key();
        if (key >= 0) {
            usermode_process_route_key(key);
            continue;
        }

        usermode_process_set_waiting_input(1);
        __asm__ volatile ("sti\n\thlt\n\tcli" : : : "memory");
        usermode_process_set_waiting_input(0);
    }
}
