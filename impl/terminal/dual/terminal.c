#include "recipes/terminal.h"
#include "recipes/ramfs.h"
#include "recipes/status.h"
#include "recipes/usermode.h"
#include "stdint.h"
#include "stddef.h"
#include "multiboot.h"

#define COM1 0x3F8
#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_ATTR 0x07
#define CURIE_WIDTH 6
#define CURIE_HEIGHT 12
#define TERMINAL_ZOOM_MAX 4
#define TERMINAL_STATUS_MAX 64
#define TERMINAL_BAR_WIDTH 14

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
static uint32_t framebuffer_foreground;
static uint32_t framebuffer_background;
static const char *framebuffer_status =
    "GRUB did not provide a framebuffer; using VGA output";
static terminal_surface terminal_surfaces[USERMODE_PROCESS_SLOTS];
static terminal_window terminal_windows[USERMODE_PROCESS_SLOTS];
static terminal_bar_position terminal_bar = TERMINAL_BAR_TOP;
static uint32_t terminal_zoom = 1;
static int terminal_config_loaded;
static char terminal_status[TERMINAL_STATUS_MAX] = "Ready";
static int left_shift;
static int right_shift;
static int left_control;
static int right_control;
static int left_alt;
static int right_alt;
static int extended_scancode;
static int serial_after_cr;
static int serial_available;
static int serial_initialized;
static int serial_escape_state;
static uint32_t serial_csi_parameters[2];
static unsigned int serial_csi_parameter_count;
static int serial_csi_invalid;
static int terminal_cursor_visible;

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

static void serial_init(void)
{
    if (serial_initialized) return;
    serial_initialized = 1;

    /* A missing UART reads all ones, which otherwise looks like RX data. */
    if (inb(COM1 + 5) == 0xFF) {
        serial_available = 0;
        return;
    }
    serial_available = 1;

    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1, 0x01);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
}

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

static void serial_putc(char c)
{
    if (!serial_available) return;
    while (!(inb(COM1 + 5) & 0x20)) {
    }
    outb(COM1, (uint8_t)c);
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
    terminal_windows[0] =
        (terminal_window){ 0, 0, columns, rows, 0, 1 };
}

static void draw_screen_cell(uint32_t row,
                             uint32_t column,
                             char character,
                             int highlighted);

static void load_terminal_config(void)
{
    if (terminal_config_loaded) return;
    terminal_config_loaded = 1;

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

    static const char path[] = "/system/config/brain.conf";
    char config[128];
    size_t length = 0;
    if (ramfs_read(path, config, sizeof(config), &length) != 0) return;

    size_t line = 0;
    while (line < length) {
        size_t end = line;
        while (end < length && config[end] != '\n' &&
               config[end] != '\r') {
            end++;
        }
        static const char key[] = "bar=";
        if (end - line >= sizeof(key) - 1 &&
            config[line] == key[0] && config[line + 1] == key[1] &&
            config[line + 2] == key[2] && config[line + 3] == key[3]) {
            size_t value_length = end - line - (sizeof(key) - 1);
            const char *value = config + line + sizeof(key) - 1;
            if (value_length == 3 && value[0] == 't' &&
                value[1] == 'o' && value[2] == 'p') {
                terminal_bar = TERMINAL_BAR_TOP;
            } else if (value_length == 6 && value[0] == 'b' &&
                       value[1] == 'o' && value[2] == 't' &&
                       value[3] == 't' && value[4] == 'o' &&
                       value[5] == 'm') {
                terminal_bar = TERMINAL_BAR_BOTTOM;
            } else if (value_length == 4 && value[0] == 'l' &&
                       value[1] == 'e' && value[2] == 'f' &&
                       value[3] == 't') {
                terminal_bar = TERMINAL_BAR_LEFT;
            } else if (value_length == 5 && value[0] == 'r' &&
                       value[1] == 'i' && value[2] == 'g' &&
                       value[3] == 'h' && value[4] == 't') {
                terminal_bar = TERMINAL_BAR_RIGHT;
            } else {
                static const char invalid[] = "Invalid bar position";
                terminal_set_status(invalid);
            }
            return;
        }
        line = end + 1;
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
    uint32_t window_count = 0;
    for (uint32_t slot = 1; slot < USERMODE_PROCESS_SLOTS; slot++) {
        if (usermode_process_window_visible(slot)) window_count++;
    }
    append_status_text(text, sizeof(text), &length, "Brain | desk ");
    append_status_number(text, sizeof(text), &length,
                         usermode_process_current_desktop());
    append_status_text(text, sizeof(text), &length, " | windows ");
    append_status_number(text, sizeof(text), &length, window_count);
    append_status_text(text, sizeof(text), &length, " | focus ");
    append_status_number(text, sizeof(text), &length,
                         usermode_process_window_pid(
                             usermode_process_focused_slot()
                         ));
    append_status_text(text, sizeof(text), &length, " | ");
    append_status_text(text, sizeof(text), &length, terminal_status);

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
            ? framebuffer_background
            : framebuffer_foreground;
        uint32_t background = highlighted
            ? framebuffer_foreground
            : framebuffer_background;
        framebuffer_draw_glyph_colors(
            row, column, (uint8_t)character, foreground, background
        );
    } else {
        uint8_t attribute = highlighted ? 0x70 : VGA_ATTR;
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
                             usermode_process_window_focused(slot));
        }
        static const char title[] = "[Eye ";
        uint32_t offset = 0;
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
    for (uint32_t slot = 1; slot < USERMODE_PROCESS_SLOTS; slot++) {
        if (usermode_process_window_visible(slot)) {
            visible[count++] = slot;
        }
    }

    for (uint32_t slot = 0; slot < USERMODE_PROCESS_SLOTS; slot++) {
        terminal_windows[slot].visible = 0;
    }
    if (count == 0 && usermode_process_current_desktop() == 0) {
        terminal_windows[0] = (terminal_window){
            origin_x, origin_y, workspace_width, workspace_height, 0, 1
        };
    } else if (count == 1) {
        uint32_t slot = visible[0];
        terminal_windows[slot] =
            (terminal_window){
                origin_x, origin_y, workspace_width, workspace_height, 0, 1
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
    refresh_window_layout();
    if (framebuffer) {
        framebuffer_clear();
    } else {
        for (uint32_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
            vga_buf[i] = ((uint16_t)VGA_ATTR << 8) | ' ';
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
    uint32_t slot = usermode_process_current_slot();
    terminal_surface *surface = &terminal_surfaces[slot];
    return (surface->columns << 16) | surface->rows;
}

void terminal_config_reload(void)
{
    terminal_config_loaded = 0;
    terminal_refresh();
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
    uint32_t slot = usermode_process_current_slot();
    initialize_surfaces();
    surface_clear(&terminal_surfaces[slot]);
    redraw_surface(slot);
    serial_init();
    if (serial_available) {
        serial_putc('\x1B');
        serial_putc('[');
        serial_putc('2');
        serial_putc('J');
        serial_putc('\x1B');
        serial_putc('[');
        serial_putc('H');
    }
}

void terminal_cursor_show(void)
{
    if (terminal_cursor_visible) return;
    terminal_cursor_visible = 1;
    draw_cursor(usermode_process_current_slot(), 1);
}

void terminal_cursor_hide(void)
{
    if (!terminal_cursor_visible) return;
    draw_cursor(usermode_process_current_slot(), 0);
    terminal_cursor_visible = 0;
}

void terminal_putc(char c)
{
    uint32_t slot = usermode_process_current_slot();
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

    if (c == '\n') serial_putc('\r');
    serial_putc(c);
}

void terminal_write(const char *s)
{
    while (*s) terminal_putc(*s++);
}

void terminal_flush(void)
{
    serial_init();
    if (!serial_available) return;
    for (uint32_t spin = 0; spin < 1000000u; spin++) {
        if (inb(COM1 + 5) & 0x40) return;
    }
}

static int scancode_to_ascii(uint8_t code)
{
    static const char normal[0x40] = {
        [0x01] = '\x1B',
        [0x01] = '\x1B',
        [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4',
        [0x06] = '5', [0x07] = '6', [0x08] = '7', [0x09] = '8',
        [0x0A] = '9', [0x0B] = '0', [0x0C] = '-', [0x0D] = '=',
        [0x0E] = '\b', [0x0F] = '\t',
        [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r',
        [0x14] = 't', [0x15] = 'y', [0x16] = 'u', [0x17] = 'i',
        [0x18] = 'o', [0x19] = 'p', [0x1A] = '[', [0x1B] = ']',
        [0x1C] = '\n', [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd',
        [0x21] = 'f', [0x22] = 'g', [0x23] = 'h', [0x24] = 'j',
        [0x25] = 'k', [0x26] = 'l', [0x27] = ';', [0x28] = '\'',
        [0x29] = '`', [0x2B] = '\\', [0x2C] = 'z', [0x2D] = 'x',
        [0x2E] = 'c', [0x2F] = 'v', [0x30] = 'b', [0x31] = 'n',
        [0x32] = 'm', [0x33] = ',', [0x34] = '.', [0x35] = '/',
        [0x39] = ' '
    };
    static const char shifted[0x40] = {
        [0x01] = '\x1B',
        [0x01] = '\x1B',
        [0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$',
        [0x06] = '%', [0x07] = '^', [0x08] = '&', [0x09] = '*',
        [0x0A] = '(', [0x0B] = ')', [0x0C] = '_', [0x0D] = '+',
        [0x0E] = '\b', [0x0F] = '\t',
        [0x10] = 'Q', [0x11] = 'W', [0x12] = 'E', [0x13] = 'R',
        [0x14] = 'T', [0x15] = 'Y', [0x16] = 'U', [0x17] = 'I',
        [0x18] = 'O', [0x19] = 'P', [0x1A] = '{', [0x1B] = '}',
        [0x1C] = '\n', [0x1E] = 'A', [0x1F] = 'S', [0x20] = 'D',
        [0x21] = 'F', [0x22] = 'G', [0x23] = 'H', [0x24] = 'J',
        [0x25] = 'K', [0x26] = 'L', [0x27] = ':', [0x28] = '"',
        [0x29] = '~', [0x2B] = '|', [0x2C] = 'Z', [0x2D] = 'X',
        [0x2E] = 'C', [0x2F] = 'V', [0x30] = 'B', [0x31] = 'N',
        [0x32] = 'M', [0x33] = '<', [0x34] = '>', [0x35] = '?',
        [0x39] = ' '
    };

    if (code >= sizeof(normal)) {
        return 0;
    }
    char character = (left_shift || right_shift) && shifted[code]
        ? shifted[code]
        : normal[code];
    if ((left_control || right_control) &&
        ((character >= 'a' && character <= 'z') ||
         (character >= 'A' && character <= 'Z'))) {
        if (character >= 'A' && character <= 'Z') character += 'a' - 'A';
        int control_character = character - 'a' + 1;
        if (control_character == '\b') return '\b';
        if (control_character == '\t') return '\t';
        if (control_character == '\n' || control_character == '\r') {
            return '\n';
        }
        return TERMINAL_KEY_CTRL_BASE + control_character;
    }
    return character;
}

static int keyboard_getchar(void)
{
    if (!(inb(0x64) & 0x01)) {
        return -1;
    }

    uint8_t code = inb(0x60);
    if (code == 0xE0) {
        extended_scancode = 1;
        return -1;
    }
    if (extended_scancode) {
        extended_scancode = 0;
        if (code == 0x38 || code == 0xB8) {
            right_alt = code == 0x38;
            return -1;
        }
        if (code == 0x1D || code == 0x9D) {
            right_control = code == 0x1D;
            return -1;
        }
        if (code == 0x4B && (left_control || right_control)) {
            return TERMINAL_KEY_CTRL_LEFT;
        }
        if (code == 0x4D && (left_control || right_control)) {
            return TERMINAL_KEY_CTRL_RIGHT;
        }
        if (code == 0x48) return TERMINAL_KEY_UP;
        if (code == 0x50) return TERMINAL_KEY_DOWN;
        if (code == 0x4B) return TERMINAL_KEY_LEFT;
        if (code == 0x4D) return TERMINAL_KEY_RIGHT;
        if (code == 0x53) return TERMINAL_KEY_DELETE;
        if (code == 0x47) return TERMINAL_KEY_HOME;
        if (code == 0x4F) return TERMINAL_KEY_END;
        return -1;
    }

    if (code == 0x2A || code == 0x36) {
        if (code == 0x2A) left_shift = 1;
        else right_shift = 1;
        return -1;
    }
    if (code == 0xAA || code == 0xB6) {
        if (code == 0xAA) left_shift = 0;
        else right_shift = 0;
        return -1;
    }
    if (code == 0x1D || code == 0x9D) {
        left_control = code == 0x1D;
        return -1;
    }
    if (code == 0x38 || code == 0xB8) {
        left_alt = code == 0x38;
        return -1;
    }
    if (code & 0x80) {
        return -1;
    }

    int character = scancode_to_ascii(code);
    if (left_alt || right_alt) {
        int shifted = left_shift || right_shift;
        if (shifted && code >= 0x02 && code <= 0x0B) {
            static const char digits[] = "1234567890";
            character = digits[code - 0x02];
        }
        return (shifted ? TERMINAL_KEY_MOD_SHIFT_BASE
                        : TERMINAL_KEY_MOD_BASE) + character;
    }
    return character;
}

lucid_status terminal_pre(void)
{
    initialize_surfaces();
    return LUCID_OK_MSG("Preparing video and input consoles");
}

void terminal_set_boot_info(const multiboot_info *info)
{
    if (!info || !(info->flags & MULTIBOOT_INFO_FRAMEBUFFER)) {
        return;
    }

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
    serial_init();
    left_shift = 0;
    right_shift = 0;
    left_control = 0;
    right_control = 0;
    left_alt = 0;
    right_alt = 0;
    extended_scancode = 0;
    serial_after_cr = 0;
    serial_escape_state = 0;
    serial_csi_parameters[0] = 0;
    serial_csi_parameters[1] = 0;
    serial_csi_parameter_count = 0;
    serial_csi_invalid = 0;
    if (framebuffer) {
        if (!serial_available) {
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

static int serial_csi_key(int c)
{
    if (c >= '0' && c <= '9') {
        if (serial_csi_parameter_count == 0) {
            serial_csi_parameter_count = 1;
        }
        unsigned int index = serial_csi_parameter_count - 1;
        uint32_t digit = (uint32_t)(c - '0');
        if (index >= 2 ||
            serial_csi_parameters[index] > (999u - digit) / 10u) {
            serial_csi_invalid = 1;
        } else {
            serial_csi_parameters[index] =
                serial_csi_parameters[index] * 10u + digit;
        }
        return -1;
    }
    if (c == ';') {
        if (serial_csi_parameter_count == 0) {
            serial_csi_parameter_count = 1;
        }
        if (serial_csi_parameter_count >= 2) {
            serial_csi_invalid = 1;
        } else {
            serial_csi_parameter_count++;
        }
        return -1;
    }

    serial_escape_state = 0;
    if (serial_csi_invalid) return -1;
    if (c == 'A' && serial_csi_parameter_count <= 1) {
        return TERMINAL_KEY_UP;
    }
    if (c == 'B' && serial_csi_parameter_count <= 1) {
        return TERMINAL_KEY_DOWN;
    }
    if (c == 'C') {
        if (serial_csi_parameter_count == 2 &&
            serial_csi_parameters[0] == 1 &&
            serial_csi_parameters[1] == 5) {
            return TERMINAL_KEY_CTRL_RIGHT;
        }
        if (serial_csi_parameter_count <= 1) return TERMINAL_KEY_RIGHT;
    }
    if (c == 'D') {
        if (serial_csi_parameter_count == 2 &&
            serial_csi_parameters[0] == 1 &&
            serial_csi_parameters[1] == 5) {
            return TERMINAL_KEY_CTRL_LEFT;
        }
        if (serial_csi_parameter_count <= 1) return TERMINAL_KEY_LEFT;
    }
    if (c == 'H' && serial_csi_parameter_count <= 1) {
        return TERMINAL_KEY_HOME;
    }
    if (c == 'F' && serial_csi_parameter_count <= 1) {
        return TERMINAL_KEY_END;
    }
    if (c == '~' && serial_csi_parameter_count == 1) {
        if (serial_csi_parameters[0] == 1 ||
            serial_csi_parameters[0] == 7) return TERMINAL_KEY_HOME;
        if (serial_csi_parameters[0] == 3) return TERMINAL_KEY_DELETE;
        if (serial_csi_parameters[0] == 4 ||
            serial_csi_parameters[0] == 8) return TERMINAL_KEY_END;
    }
    return -1;
}

static int terminal_poll_key(void)
{
    for (;;) {
        int c;
        if (serial_available && (inb(COM1 + 5) & 0x01)) {
            c = inb(COM1);
        } else {
            return keyboard_getchar();
        }

        if (serial_escape_state == 1) {
            if (c == '[') {
                serial_escape_state = 2;
                serial_csi_parameters[0] = 0;
                serial_csi_parameters[1] = 0;
                serial_csi_parameter_count = 0;
                serial_csi_invalid = 0;
                continue;
            }
            serial_escape_state = 0;
            if (c == '\r' || c == '\n') {
                return TERMINAL_KEY_MOD_BASE + '\n';
            }
            if (c >= 'A' && c <= 'Z') {
                return TERMINAL_KEY_MOD_SHIFT_BASE + c;
            }
            int shifted_digit = 1;
            if (c == '!') c = '1';
            else if (c == '@') c = '2';
            else if (c == '#') c = '3';
            else if (c == '$') c = '4';
            else if (c == '%') c = '5';
            else if (c == '^') c = '6';
            else if (c == '&') c = '7';
            else if (c == '*') c = '8';
            else if (c == '(') c = '9';
            else if (c == ')') c = '0';
            else shifted_digit = 0;
            return (shifted_digit
                ? TERMINAL_KEY_MOD_SHIFT_BASE
                : TERMINAL_KEY_MOD_BASE) + c;
        }
        if (serial_escape_state == 2) {
            int key = serial_csi_key(c);
            if (key >= 0) return key;
            continue;
        }
        if (c == 0x1B) {
            serial_escape_state = 1;
            continue;
        }
        if (c == '\r') {
            serial_after_cr = 1;
            return '\n';
        }
        if (c == '\n' && serial_after_cr) {
            serial_after_cr = 0;
            continue;
        }
        serial_after_cr = 0;
        if (c >= 1 && c <= 26 &&
            c != '\b' && c != '\t' && c != '\n' && c != '\r') {
            return TERMINAL_KEY_CTRL_BASE + c;
        }
        return c == 0x7F ? '\b' : c;
    }
}

int terminal_getchar(void)
{
    for (;;) {
        int queued = usermode_process_read_key();
        if (queued >= 0) return queued;

        int key = terminal_poll_key();
        if (key >= 0) {
            usermode_process_route_key(key);
            continue;
        }

        usermode_process_set_waiting_input(1);
        __asm__ volatile ("sti\n\thlt\n\tcli" : : : "memory");
        usermode_process_set_waiting_input(0);
    }
}
