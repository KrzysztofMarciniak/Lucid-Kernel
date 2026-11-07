#include "../clearsh/clearsh_internal.h"

#define CONFIG_PATH "/system/config/brain/statusbar"
#define DEFAULT_OUTPUT_PATH "/system/config/brain/statusbar_output"
#define CONFIG_MAX 512u
#define STATUS_OUTPUT_MAX 128u
#define CONFIG_POLL_TICKS 100u

typedef struct {
    int enabled;
    uint32_t interval_ticks;
    char script[PATH_MAX];
    char output[PATH_MAX];
} statusbar_config;

static int copy_config_value(const char *value,
                             size_t length,
                             char *output,
                             size_t capacity)
{
    if (!length || length >= capacity) return 0;
    for (size_t i = 0; i < length; i++) {
        if (value[i] <= ' ' || value[i] == 0x7F) return 0;
        output[i] = value[i];
    }
    output[length] = '\0';
    return 1;
}

static int config_field(const char *config,
                        size_t length,
                        const char *name,
                        const char **value,
                        size_t *value_length)
{
    size_t name_length = string_length(name);
    size_t offset = 0;
    while (offset < length) {
        size_t end = offset;
        while (end < length && config[end] != '\n') end++;
        size_t start = offset;
        while (start < end &&
               (config[start] == ' ' || config[start] == '\t')) start++;
        size_t equals = start;
        while (equals < end && config[equals] != '=') equals++;
        size_t key_end = equals;
        while (key_end > start &&
               (config[key_end - 1] == ' ' ||
                config[key_end - 1] == '\t')) key_end--;
        if (equals < end && key_end - start == name_length) {
            size_t i = 0;
            while (i < name_length && config[start + i] == name[i]) i++;
            if (i == name_length) {
                start = equals + 1;
                while (start < end &&
                       (config[start] == ' ' || config[start] == '\t')) start++;
                while (end > start &&
                       (config[end - 1] == ' ' ||
                        config[end - 1] == '\t' ||
                        config[end - 1] == '\r')) end--;
                *value = config + start;
                *value_length = end - start;
                return 1;
            }
        }
        offset = end + 1;
    }
    return 0;
}

static int parse_enabled(const char *value, size_t length, int *enabled)
{
    if (length != 1 || (value[0] != '0' && value[0] != '1')) return 0;
    *enabled = value[0] == '1';
    return 1;
}

static int parse_interval(const char *value,
                          size_t length,
                          uint32_t *interval_ticks)
{
    if (!length) return 0;
    uint32_t milliseconds = 0;
    for (size_t i = 0; i < length; i++) {
        if (value[i] < '0' || value[i] > '9') return 0;
        uint32_t digit = (uint32_t)(value[i] - '0');
        if (milliseconds > (0xFFFFFFFFu - digit) / 10u) return 0;
        milliseconds = milliseconds * 10u + digit;
    }
    if (!milliseconds) return 0;
    *interval_ticks = milliseconds / 10u;
    if (milliseconds % 10u) (*interval_ticks)++;
    return *interval_ticks != 0;
}

static int load_config(statusbar_config *result)
{
    char contents[CONFIG_MAX];
    size_t length = 0;
    if (ramfs_read(CONFIG_PATH, contents, sizeof(contents), &length) ||
        length >= sizeof(contents)) {
        return 0;
    }

    const char *value;
    size_t value_length;
    if (!config_field(contents, length, "enabled", &value, &value_length) ||
        !parse_enabled(value, value_length, &result->enabled)) {
        return 0;
    }
    if (!config_field(contents, length, "interval_ms", &value, &value_length) ||
        !parse_interval(value, value_length, &result->interval_ticks)) {
        return 0;
    }
    if (!config_field(contents, length, "script", &value, &value_length) ||
        !copy_config_value(value, value_length, result->script,
                           sizeof(result->script)) ||
        result->script[0] != '/') {
        return 0;
    }
    if (!config_field(contents, length, "output", &value, &value_length) ||
        !copy_config_value(value, value_length, result->output,
                           sizeof(result->output)) ||
        result->output[0] != '/') {
        return 0;
    }
    return 1;
}

static int same_config(const statusbar_config *left,
                       const statusbar_config *right)
{
    return left->enabled == right->enabled &&
        left->interval_ticks == right->interval_ticks &&
        string_equal(left->script, right->script) &&
        string_equal(left->output, right->output);
}

static void write_output(const char *path, const char *text)
{
    size_t length = string_length(text);
    if (length > STATUS_OUTPUT_MAX) length = STATUS_OUTPUT_MAX;
    (void)ramfs_write(path, text, length);
}

static void write_error(const char *output, const char *message)
{
    static const char prefix[] = "status script error: ";
    char text[STATUS_OUTPUT_MAX];
    size_t used = 0;
    while (prefix[used] && used + 1 < sizeof(text)) {
        text[used] = prefix[used];
        used++;
    }
    for (size_t i = 0; message[i] && used + 1 < sizeof(text); i++) {
        text[used++] = message[i];
    }
    text[used] = '\0';
    write_output(output, text);
}

static void sleep_ticks(uint32_t ticks)
{
    (void)clearsh_syscall(SYS_SLEEP, ticks, 0, 0, 0);
}

__attribute__((section(".entry")))
void statusbar_user_entry(void)
{
    statusbar_config config;
    statusbar_config previous;
    int have_previous = 0;
    uint32_t next_run = 0;

    for (;;) {
        if (!load_config(&config)) {
            write_error(DEFAULT_OUTPUT_PATH, "invalid statusbar config");
            sleep_ticks(CONFIG_POLL_TICKS);
            have_previous = 0;
            continue;
        }

        if (!config.enabled) {
            write_output(config.output, "");
            sleep_ticks(CONFIG_POLL_TICKS);
            have_previous = 0;
            continue;
        }

        if (!have_previous || !same_config(&config, &previous)) {
            previous = config;
            have_previous = 1;
            next_run = clearsh_syscall(SYS_GETTICKS, 0, 0, 0, 0);
        }

        uint32_t now = clearsh_syscall(SYS_GETTICKS, 0, 0, 0, 0);
        if ((int32_t)(now - next_run) >= 0) {
            value result = clearsh_run_statusbar_script(config.script);
            if (result.type == VALUE_ERROR) {
                write_error(config.output, result.data.text);
            }
            now = clearsh_syscall(SYS_GETTICKS, 0, 0, 0, 0);
            next_run = now + config.interval_ticks;
        }

        now = clearsh_syscall(SYS_GETTICKS, 0, 0, 0, 0);
        uint32_t remaining = next_run - now;
        if ((int32_t)remaining <= 0 || remaining > CONFIG_POLL_TICKS) {
            remaining = CONFIG_POLL_TICKS;
        }
        sleep_ticks(remaining);
    }
}
