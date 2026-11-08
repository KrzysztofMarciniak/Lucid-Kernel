#include "window_config.h"
#include "recipes/ramfs.h"
#include "recipes/terminal.h"
#include "recipes/usermode.h"
#include "stddef.h"

#define BRAIN_CONFIG_MAX 512

static int brain_config_value(const char *config,
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

static int parse_config_number(const char *value,
                               size_t length,
                               uint32_t *number)
{
    if (length == 0) return 0;
    uint32_t result = 0;
    for (size_t i = 0; i < length; i++) {
        if (value[i] < '0' || value[i] > '9') return 0;
        uint32_t digit = (uint32_t)(value[i] - '0');
        if (result > (0xFFFFFFFFu - digit) / 10u) return 0;
        result = result * 10u + digit;
    }
    *number = result;
    return 1;
}

static int parse_key_binding(const char *value,
                             size_t length,
                             uint32_t *binding)
{
    static const char alt_prefix[] = "alt+";
    static const char shift_prefix[] = "alt+shift+";
    if (length == 9) {
        static const char digits[] = "alt+digit";
        size_t i = 0;
        while (i < length && value[i] == digits[i]) i++;
        if (i == length) {
            *binding = TERMINAL_KEY_MOD_BASE;
            return 1;
        }
    }
    if (length == 15) {
        static const char digits[] = "alt+shift+digit";
        size_t i = 0;
        while (i < length && value[i] == digits[i]) i++;
        if (i == length) {
            *binding = TERMINAL_KEY_MOD_SHIFT_BASE;
            return 1;
        }
    }

    size_t prefix_length;
    uint32_t key_base;
    if (length > sizeof(shift_prefix) - 1) {
        prefix_length = sizeof(shift_prefix) - 1;
        key_base = TERMINAL_KEY_MOD_SHIFT_BASE;
        size_t i = 0;
        while (i < prefix_length && value[i] == shift_prefix[i]) i++;
        if (i != prefix_length) return 0;
    } else {
        prefix_length = sizeof(alt_prefix) - 1;
        key_base = TERMINAL_KEY_MOD_BASE;
        if (length <= prefix_length) return 0;
        size_t i = 0;
        while (i < prefix_length && value[i] == alt_prefix[i]) i++;
        if (i != prefix_length) return 0;
    }

    size_t key_length = length - prefix_length;
    if (key_length == 5 && value[prefix_length] == 'e' &&
        value[prefix_length + 1] == 'n' &&
        value[prefix_length + 2] == 't' &&
        value[prefix_length + 3] == 'e' &&
        value[prefix_length + 4] == 'r') {
        *binding = key_base + '\n';
        return 1;
    }
    if (key_length != 1) return 0;
    char key = value[prefix_length];
    if (key >= 'A' && key <= 'Z') key += 'a' - 'A';
    if (key < '!' || key > '~' || key == '+') return 0;
    *binding = key_base + (uint32_t)key;
    return 1;
}

static void load_key_binding(const char *config,
                             size_t length,
                             const char *name,
                             uint32_t *binding)
{
    const char *value;
    size_t value_length;
    uint32_t parsed;
    if (!brain_config_value(config, length, name,
                            &value, &value_length)) {
        return;
    }
    if (parse_key_binding(value, value_length, &parsed) &&
        parsed != TERMINAL_KEY_MOD_BASE &&
        parsed != TERMINAL_KEY_MOD_SHIFT_BASE) {
        *binding = parsed;
    } else {
        terminal_set_status("Invalid Brain key binding");
    }
}

void process_window_config_load(process_window_config *config)
{
    config->desktop_count = PROCESS_DESKTOP_LIMIT;
    config->startup_desktop = 2;
    config->log_desktop = 1;
    config->master_ratio = 60;
    config->master_min_ratio = 30;
    config->master_max_ratio = 80;
    config->master_step = 5;
    config->key_open = TERMINAL_KEY_MOD_BASE + '\n';
    config->key_close = TERMINAL_KEY_MOD_BASE + 'q';
    config->key_focus_next = TERMINAL_KEY_MOD_BASE + 'j';
    config->key_focus_previous = TERMINAL_KEY_MOD_BASE + 'k';
    config->key_exit = TERMINAL_KEY_MOD_SHIFT_BASE + 'q';
    config->key_master_shrink = TERMINAL_KEY_MOD_SHIFT_BASE + 'h';
    config->key_master_grow = TERMINAL_KEY_MOD_SHIFT_BASE + 'l';
    config->key_switch_desktop = TERMINAL_KEY_MOD_BASE;
    config->key_move_desktop = TERMINAL_KEY_MOD_SHIFT_BASE;

    static const char layout_path[] = "/system/config/brain/layout";
    static const char keys_path[] = "/system/config/brain/keys";
    char layout[BRAIN_CONFIG_MAX];
    char keys[BRAIN_CONFIG_MAX];
    size_t layout_length = 0;
    size_t keys_length = 0;
    if (ramfs_read(layout_path, layout, sizeof(layout),
                   &layout_length) != 0 ||
        layout_length >= sizeof(layout) ||
        ramfs_read(keys_path, keys, sizeof(keys), &keys_length) != 0 ||
        keys_length >= sizeof(keys)) {
        terminal_set_status("Brain config unreadable or too large");
        return;
    }

    const char *value;
    size_t value_length;
    uint32_t number;
    if (brain_config_value(layout, layout_length, "desktops",
                           &value, &value_length)) {
        if (parse_config_number(value, value_length, &number) &&
            number >= 1 && number <= PROCESS_DESKTOP_LIMIT) {
            config->desktop_count = number;
        } else {
            terminal_set_status("Invalid Brain desktop count");
        }
    }
    if (brain_config_value(layout, layout_length, "startup_desktop",
                           &value, &value_length)) {
        if (parse_config_number(value, value_length, &number) &&
            number < config->desktop_count) {
            config->startup_desktop = number;
        } else {
            terminal_set_status("Invalid Brain startup desktop");
        }
    }
    if (brain_config_value(layout, layout_length, "log_desktop",
                           &value, &value_length)) {
        if (parse_config_number(value, value_length, &number) &&
            number < config->desktop_count) {
            config->log_desktop = number;
        } else {
            terminal_set_status("Invalid Brain log desktop");
        }
    }
    if (config->startup_desktop >= config->desktop_count) {
        config->startup_desktop = config->desktop_count > 2 ? 2 : 0;
    }
    if (config->log_desktop >= config->desktop_count) {
        config->log_desktop = config->desktop_count > 1 ? 1 : 0;
    }
    if (brain_config_value(layout, layout_length, "master_ratio",
                           &value, &value_length)) {
        if (parse_config_number(value, value_length, &number) &&
            number >= 1 && number <= 99) {
            config->master_ratio = number;
        } else {
            terminal_set_status("Invalid Brain master ratio");
        }
    }
    if (brain_config_value(layout, layout_length, "master_min_ratio",
                           &value, &value_length)) {
        if (parse_config_number(value, value_length, &number) &&
            number >= 1 && number <= 99) {
            config->master_min_ratio = number;
        } else {
            terminal_set_status("Invalid Brain minimum master ratio");
        }
    }
    if (brain_config_value(layout, layout_length, "master_max_ratio",
                           &value, &value_length)) {
        if (parse_config_number(value, value_length, &number) &&
            number >= 1 && number <= 99) {
            config->master_max_ratio = number;
        } else {
            terminal_set_status("Invalid Brain maximum master ratio");
        }
    }
    if (brain_config_value(layout, layout_length, "master_step",
                           &value, &value_length)) {
        if (parse_config_number(value, value_length, &number) &&
            number >= 1 && number <= 99) {
            config->master_step = number;
        } else {
            terminal_set_status("Invalid Brain master step");
        }
    }
    if (config->master_min_ratio >= config->master_max_ratio) {
        config->master_min_ratio = 30;
        config->master_max_ratio = 80;
        terminal_set_status("Invalid Brain master ratio limits");
    }
    if (config->master_ratio < config->master_min_ratio ||
        config->master_ratio > config->master_max_ratio) {
        config->master_ratio =
            config->master_ratio < config->master_min_ratio
                ? config->master_min_ratio
                : config->master_max_ratio;
        terminal_set_status("Brain master ratio clamped to limits");
    }

    load_key_binding(keys, keys_length, "key_open", &config->key_open);
    load_key_binding(keys, keys_length, "key_close", &config->key_close);
    load_key_binding(keys, keys_length,
                     "key_focus_next", &config->key_focus_next);
    load_key_binding(keys, keys_length,
                     "key_focus_previous", &config->key_focus_previous);
    load_key_binding(keys, keys_length, "key_exit", &config->key_exit);
    load_key_binding(keys, keys_length,
                     "key_master_shrink", &config->key_master_shrink);
    load_key_binding(keys, keys_length,
                     "key_master_grow", &config->key_master_grow);

    if (brain_config_value(keys, keys_length, "key_switch_desktop",
                           &value, &value_length)) {
        uint32_t binding;
        if (parse_key_binding(value, value_length, &binding) &&
            (binding == TERMINAL_KEY_MOD_BASE ||
             binding == TERMINAL_KEY_MOD_SHIFT_BASE)) {
            config->key_switch_desktop = binding;
        } else {
            terminal_set_status("Invalid Brain desktop key binding");
        }
    }
    if (brain_config_value(keys, keys_length, "key_move_desktop",
                           &value, &value_length)) {
        uint32_t binding;
        if (parse_key_binding(value, value_length, &binding) &&
            (binding == TERMINAL_KEY_MOD_BASE ||
             binding == TERMINAL_KEY_MOD_SHIFT_BASE)) {
            config->key_move_desktop = binding;
        } else {
            terminal_set_status("Invalid Brain move key binding");
        }
    }
    if (config->key_switch_desktop == config->key_move_desktop) {
        config->key_switch_desktop = TERMINAL_KEY_MOD_BASE;
        config->key_move_desktop = TERMINAL_KEY_MOD_SHIFT_BASE;
        terminal_set_status("Brain desktop bindings must differ");
    }
}
