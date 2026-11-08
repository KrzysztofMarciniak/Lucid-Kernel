#include "process_internal.h"
#include "window_config.h"
#include "window_manager.h"
#include "recipes/syscall.h"
#include "recipes/terminal.h"
#include "recipes/usermode.h"

static uint32_t focused_slot;
static uint32_t manager_slot;
static uint32_t current_desktop = 1;
static uint32_t master_ratio = 60;
static uint32_t master_min_ratio = 30;
static uint32_t master_max_ratio = 80;
static uint32_t master_step = 5;
static uint32_t desktop_count = PROCESS_DESKTOP_LIMIT;
static uint32_t startup_desktop = 2;
static uint32_t log_desktop = 1;
static uint32_t key_open = TERMINAL_KEY_MOD_BASE + '\n';
static uint32_t key_close = TERMINAL_KEY_MOD_BASE + 'q';
static uint32_t key_focus_next = TERMINAL_KEY_MOD_BASE + 'j';
static uint32_t key_focus_previous = TERMINAL_KEY_MOD_BASE + 'k';
static uint32_t key_exit = TERMINAL_KEY_MOD_SHIFT_BASE + 'q';
static uint32_t key_master_shrink = TERMINAL_KEY_MOD_SHIFT_BASE + 'h';
static uint32_t key_master_grow = TERMINAL_KEY_MOD_SHIFT_BASE + 'l';
static uint32_t key_switch_desktop = TERMINAL_KEY_MOD_BASE;
static uint32_t key_move_desktop = TERMINAL_KEY_MOD_SHIFT_BASE;

static int process_is_visible_window(uint32_t slot)
{
    return usermode_process_window_active(slot) &&
        usermode_process_window_desktop_raw(slot) == current_desktop;
}

static uint32_t next_window_slot(uint32_t from, int direction)
{
    for (uint32_t offset = 1; offset <= USERMODE_PROCESS_SLOTS; offset++) {
        uint32_t slot;
        if (direction > 0) {
            slot = (from + offset) % USERMODE_PROCESS_SLOTS;
        } else {
            slot = (from + USERMODE_PROCESS_SLOTS - offset) %
                USERMODE_PROCESS_SLOTS;
        }
        if (process_is_visible_window(slot)) return slot;
    }
    return 0;
}

static void close_window(uint32_t slot)
{
    if (!usermode_process_window_active(slot)) return;
    terminal_set_status("Killing terminal");
    usermode_process_close_window(slot);
}

static int handle_brain_binding(uint32_t key)
{
    if (key == key_open) {
        terminal_set_status("Starting terminal");
        usermode_process_enqueue_key(manager_slot, SYS_WM_OPEN);
    } else if (key == key_close) {
        close_window(focused_slot);
    } else if (key == key_focus_next) {
        focused_slot = next_window_slot(focused_slot, 1);
        terminal_set_status("Focus changed");
    } else if (key == key_focus_previous) {
        focused_slot = next_window_slot(focused_slot, -1);
        terminal_set_status("Focus changed");
    } else if (key == key_exit) {
        terminal_set_status("Brain exiting");
        for (uint32_t slot = 1; slot < USERMODE_PROCESS_SLOTS; slot++) {
            close_window(slot);
        }
        terminal_set_status("Brain exiting");
        usermode_process_enqueue_key(manager_slot, SYS_WM_EXIT);
    } else if (key == key_master_shrink &&
               master_ratio > master_min_ratio) {
        master_ratio = master_ratio - master_step < master_min_ratio
            ? master_min_ratio
            : master_ratio - master_step;
        terminal_set_status("Master resized");
    } else if (key == key_master_grow &&
               master_ratio < master_max_ratio) {
        master_ratio = master_ratio + master_step > master_max_ratio
            ? master_max_ratio
            : master_ratio + master_step;
        terminal_set_status("Master resized");
    } else {
        return 0;
    }
    return 1;
}

static void switch_desktop(uint32_t desktop)
{
    current_desktop = desktop;
    focused_slot = next_window_slot(USERMODE_PROCESS_SLOTS - 1, 1);
    terminal_set_status("Desktop changed");
}

static void move_focused_window(uint32_t desktop)
{
    if (!usermode_process_window_exists(focused_slot)) return;
    usermode_process_set_window_desktop(focused_slot, desktop);
    if (desktop != current_desktop) {
        focused_slot = next_window_slot(focused_slot, 1);
    }
    terminal_set_status("Window moved");
}

void usermode_process_route_key(int key)
{
    if (key >= TERMINAL_KEY_MOD_BASE &&
        key < TERMINAL_KEY_MOD_SHIFT_BASE) {
        int command = key - TERMINAL_KEY_MOD_BASE;
        if (manager_slot == 0) return;
        if (command >= '0' && command <= '9' &&
            (uint32_t)(command - '0') < desktop_count &&
            key_switch_desktop == TERMINAL_KEY_MOD_BASE) {
            switch_desktop((uint32_t)(command - '0'));
        } else if (command >= '0' && command <= '9' &&
                   (uint32_t)(command - '0') < desktop_count &&
                   key_move_desktop == TERMINAL_KEY_MOD_BASE) {
            move_focused_window((uint32_t)(command - '0'));
        } else {
            (void)handle_brain_binding((uint32_t)key);
        }
        terminal_refresh();
        return;
    }
    if (key >= TERMINAL_KEY_MOD_SHIFT_BASE &&
        key < TERMINAL_KEY_MOD_SHIFT_BASE + 0x100) {
        int command = key - TERMINAL_KEY_MOD_SHIFT_BASE;
        if (manager_slot == 0) return;
        if (command >= 'A' && command <= 'Z') {
            command += 'a' - 'A';
        }
        uint32_t normalized_key =
            TERMINAL_KEY_MOD_SHIFT_BASE + (uint32_t)command;
        if (command >= '0' && command <= '9' &&
            (uint32_t)(command - '0') < desktop_count &&
            key_move_desktop == TERMINAL_KEY_MOD_SHIFT_BASE) {
            move_focused_window((uint32_t)(command - '0'));
        } else if (command >= '0' && command <= '9' &&
                   (uint32_t)(command - '0') < desktop_count &&
                   key_switch_desktop == TERMINAL_KEY_MOD_SHIFT_BASE) {
            switch_desktop((uint32_t)(command - '0'));
        } else {
            (void)handle_brain_binding(normalized_key);
        }
        terminal_refresh();
        return;
    }

    uint32_t slot = focused_slot;
    if (!process_is_visible_window(slot)) slot = 0;
    usermode_process_enqueue_key(slot, (uint32_t)key);
}

void usermode_process_config_reload(void)
{
    process_window_config config;
    process_window_config_load(&config);
    desktop_count = config.desktop_count;
    startup_desktop = config.startup_desktop;
    log_desktop = config.log_desktop;
    master_ratio = config.master_ratio;
    master_min_ratio = config.master_min_ratio;
    master_max_ratio = config.master_max_ratio;
    master_step = config.master_step;
    key_open = config.key_open;
    key_close = config.key_close;
    key_focus_next = config.key_focus_next;
    key_focus_previous = config.key_focus_previous;
    key_exit = config.key_exit;
    key_master_shrink = config.key_master_shrink;
    key_master_grow = config.key_master_grow;
    key_switch_desktop = config.key_switch_desktop;
    key_move_desktop = config.key_move_desktop;
    if (current_desktop >= desktop_count) current_desktop = 0;
    for (uint32_t slot = 1; slot < USERMODE_PROCESS_SLOTS; slot++) {
        if (usermode_process_window_exists(slot) &&
            usermode_process_window_desktop_raw(slot) >= desktop_count) {
            usermode_process_set_window_desktop(slot, desktop_count - 1);
        }
    }
}

void window_manager_manager_started(uint32_t slot)
{
    manager_slot = slot;
    focused_slot = 0;
    usermode_process_config_reload();
    terminal_config_reload();
    current_desktop = startup_desktop;
    terminal_refresh();
}

void window_manager_window_started(uint32_t slot)
{
    usermode_process_set_window_desktop(slot, current_desktop);
    focused_slot = slot;
    terminal_refresh();
}

void window_manager_process_exited(uint32_t slot)
{
    if (focused_slot == slot) focused_slot = next_window_slot(slot, 1);
    usermode_process_enqueue_key(manager_slot, SYS_WM_REAP);
    terminal_refresh();
}

void window_manager_process_released(uint32_t slot)
{
    if (manager_slot == slot) manager_slot = 0;
}

uint32_t usermode_process_focused_slot(void)
{
    return focused_slot;
}

int usermode_process_window_visible(uint32_t slot)
{
    return process_is_visible_window(slot);
}

int usermode_process_window_focused(uint32_t slot)
{
    return process_is_visible_window(slot) && slot == focused_slot;
}

uint32_t usermode_process_current_desktop(void)
{
    return current_desktop;
}

uint32_t usermode_process_desktop_count(void)
{
    return desktop_count;
}

uint32_t usermode_process_window_desktop(uint32_t slot)
{
    return usermode_process_window_exists(slot)
        ? usermode_process_window_desktop_raw(slot)
        : current_desktop;
}

uint32_t usermode_process_master_ratio(void)
{
    return master_ratio;
}

uint32_t usermode_process_log_desktop(void)
{
    return log_desktop;
}
