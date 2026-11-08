#ifndef LUCID_IMPL_USERMODE_IRET3_WINDOW_CONFIG_H
#define LUCID_IMPL_USERMODE_IRET3_WINDOW_CONFIG_H

#include "stdint.h"

#define PROCESS_DESKTOP_LIMIT 10u

typedef struct {
    uint32_t desktop_count;
    uint32_t startup_desktop;
    uint32_t log_desktop;
    uint32_t master_ratio;
    uint32_t master_min_ratio;
    uint32_t master_max_ratio;
    uint32_t master_step;
    uint32_t key_open;
    uint32_t key_close;
    uint32_t key_focus_next;
    uint32_t key_focus_previous;
    uint32_t key_exit;
    uint32_t key_master_shrink;
    uint32_t key_master_grow;
    uint32_t key_switch_desktop;
    uint32_t key_move_desktop;
} process_window_config;

void process_window_config_load(process_window_config *config);

#endif
