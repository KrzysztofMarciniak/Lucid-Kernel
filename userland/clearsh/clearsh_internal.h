#ifndef CLEARSH_INTERNAL_H
#define CLEARSH_INTERNAL_H

#include <stddef.h>
#include <stdint.h>
#include "recipes/syscall.h"

#define TEXT_MAX (SYS_FILE_MAX + 1)
#define PATH_MAX SYS_PATH_MAX
#define MAX_ARGS 8
#define MAX_DEPTH 8
#define ARENA_SIZE 64
#define LINE_MAX SYS_LINE_MAX
#define EDITOR_MAX SYS_FILE_MAX

typedef enum {
    VALUE_NIL,
    VALUE_TEXT,
    VALUE_NUMBER,
    VALUE_BOOLEAN,
    VALUE_PATH,
    VALUE_FILE,
    VALUE_LIST,
    VALUE_FUNCTION,
    VALUE_LAMBDA,
    VALUE_ERROR
} value_type;

typedef enum {
    FUNCTION_FILE,
    FUNCTION_READ,
    FUNCTION_WRITE,
    FUNCTION_CLEAR,
    FUNCTION_PRINT,
    FUNCTION_TEXT,
    FUNCTION_LIST,
    FUNCTION_MAP,
    FUNCTION_FILTER,
    FUNCTION_RUN,
    FUNCTION_HELP,
    FUNCTION_LS,
    FUNCTION_CD,
    FUNCTION_PWD,
    FUNCTION_LAMBDA,
    FUNCTION_EDIT,
    FUNCTION_INSTALL,
    FUNCTION_EXIT,
    FUNCTION_SHUTDOWN,
    FUNCTION_SYSCALL,
    FUNCTION_ADD,
    FUNCTION_SUBTRACT,
    FUNCTION_MULTIPLY,
    FUNCTION_DIVIDE,
    FUNCTION_MODULO,
    FUNCTION_ABS,
    FUNCTION_MIN,
    FUNCTION_MAX,
    FUNCTION_POWER,
    FUNCTION_EQUAL,
    FUNCTION_LESS,
    FUNCTION_LESS_EQUAL,
    FUNCTION_GREATER,
    FUNCTION_GREATER_EQUAL,
    FUNCTION_UPTIME,
    FUNCTION_NOT,
    FUNCTION_AND,
    FUNCTION_OR,
    FUNCTION_IF,
    FUNCTION_LENGTH,
    FUNCTION_CONCAT
} function_id;

typedef struct value value;

struct value {
    value_type type;
    union {
        char text[TEXT_MAX];
        int32_t number;
        int boolean;
        function_id function;
        struct {
            value *items;
            size_t count;
        } list;
        struct {
            char parameter[PATH_MAX];
            const char *body;
            size_t body_length;
        } lambda;
    } data;
};



extern value arena[ARENA_SIZE];
extern size_t arena_used;
extern char current_directory[PATH_MAX];
extern char pending_script_path[PATH_MAX];
extern int script_pending;
extern int clearsh_quiet_output;

void terminal_write(const char *text);
void terminal_putc(char c);
uint32_t terminal_read_key(void);
uint32_t terminal_get_size(void);
int terminal_file_clear(void);
int ramfs_read(const char *path, char *buffer, size_t capacity, size_t *length);
int ramfs_write(const char *path, const char *data, size_t length);
int ramfs_list(const char *path, char *buffer, size_t capacity, size_t *length);
int ramfs_is_directory(const char *path);

size_t string_length(const char *s);
int string_equal(const char *a, const char *b);
void string_copy(char *dst, const char *src, size_t capacity);
value make_value(value_type type);
value make_error(const char *message);
int value_to_text(const value *v, char *buffer, size_t capacity);
void print_value(const value *v);
int truthy(const value *v);
int resolve_path(const char *input, char *output, size_t capacity);
int get_number(const value *item, int32_t *number);
value make_number(int32_t number);
int checked_number(int64_t number, int32_t *result);
int equal_values(const value *left, const value *right);
int clearsh_function_candidates(char *buffer,
                                size_t capacity,
                                size_t *length);

value evaluate_lambda(const value *lambda, const value *argument);
value evaluate_line(const char *line);
value call_function(function_id id, value *args, size_t count);
value edit_file(const value *file);
value run_pending_script(void);
value clearsh_run_statusbar_script(const char *path);

#endif
