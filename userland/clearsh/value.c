#include "clearsh_internal.h"

size_t string_length(const char *s)
{
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

int string_equal(const char *a, const char *b)
{
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

void string_copy(char *dst, const char *src, size_t capacity)
{
    size_t i = 0;
    while (i + 1 < capacity && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

value make_value(value_type type)
{
    value result;
    for (size_t i = 0; i < sizeof(result); i++) {
        ((uint8_t *)&result)[i] = 0;
    }
    result.type = type;
    return result;
}

value make_error(const char *message)
{
    value result = make_value(VALUE_ERROR);
    string_copy(result.data.text, message, sizeof(result.data.text));
    return result;
}

int value_to_text(const value *v, char *buffer, size_t capacity)
{
    if (capacity == 0) return 0;

    if (v->type == VALUE_TEXT || v->type == VALUE_PATH ||
        v->type == VALUE_FILE) {
        string_copy(buffer, v->data.text, capacity);
        return string_length(v->data.text) < capacity;
    }
    if (v->type == VALUE_BOOLEAN) {
        string_copy(buffer, v->data.boolean ? "true" : "false", capacity);
        return 1;
    }
    if (v->type == VALUE_NIL) {
        string_copy(buffer, "nil", capacity);
        return 1;
    }
    if (v->type != VALUE_NUMBER) return 0;

    char reverse[12];
    size_t n = 0;
    int32_t number = v->data.number;
    uint32_t magnitude = number < 0 ? (uint32_t)(-(number + 1)) + 1 : (uint32_t)number;
    do {
        reverse[n++] = (char)('0' + magnitude % 10);
        magnitude /= 10;
    } while (magnitude && n < sizeof(reverse));

    size_t needed = n + (number < 0 ? 1 : 0);
    if (needed + 1 > capacity) return 0;
    size_t used = 0;
    if (number < 0) buffer[used++] = '-';
    while (n) buffer[used++] = reverse[--n];
    buffer[used] = '\0';
    return 1;
}

void print_value(const value *v)
{
    char number[16];
    switch (v->type) {
    case VALUE_NIL:
        terminal_write("nil");
        break;
    case VALUE_TEXT:
        terminal_write(v->data.text);
        break;
    case VALUE_PATH:
        terminal_write(v->data.text);
        break;
    case VALUE_FILE:
        terminal_write("<file ");
        terminal_write(v->data.text);
        terminal_putc('>');
        break;
    case VALUE_NUMBER: {
        value_to_text(v, number, sizeof(number));
        terminal_write(number);
        break;
    }
    case VALUE_BOOLEAN:
        terminal_write(v->data.boolean ? "true" : "false");
        break;
    case VALUE_LIST:
        terminal_putc('(');
        for (size_t i = 0; i < v->data.list.count; i++) {
            if (i) terminal_putc(' ');
            print_value(&v->data.list.items[i]);
        }
        terminal_putc(')');
        break;
    case VALUE_FUNCTION:
        terminal_write("<function>");
        break;
    case VALUE_LAMBDA:
        terminal_write("<lambda>");
        break;
    case VALUE_ERROR:
        terminal_write("error: ");
        terminal_write(v->data.text);
        break;
    }
}

int truthy(const value *v)
{
    if (v->type == VALUE_NIL) return 0;
    if (v->type == VALUE_BOOLEAN) return v->data.boolean;
    if (v->type == VALUE_NUMBER) return v->data.number != 0;
    if (v->type == VALUE_TEXT) return v->data.text[0] != '\0';
    return 1;
}

int resolve_path(const char *input, char *output, size_t capacity)
{
    char joined[PATH_MAX * 2];
    size_t used = 0;

    if (input[0] == '/') {
        string_copy(joined, input, sizeof(joined));
    } else {
        size_t cwd_length = string_length(current_directory);
        for (size_t i = 0; i < cwd_length; i++) joined[used++] = current_directory[i];
        if (used > 1) joined[used++] = '/';
        for (size_t i = 0; input[i] && used + 1 < sizeof(joined); i++) {
            joined[used++] = input[i];
        }
        joined[used] = '\0';
    }

    output[0] = '/';
    output[1] = '\0';
    size_t output_length = 1;
    size_t pos = 0;
    while (joined[pos]) {
        while (joined[pos] == '/') pos++;
        if (!joined[pos]) break;
        size_t start = pos;
        while (joined[pos] && joined[pos] != '/') pos++;
        size_t part_length = pos - start;

        if (part_length == 1 && joined[start] == '.') continue;
        if (part_length == 2 && joined[start] == '.' && joined[start + 1] == '.') {
            if (output_length > 1) {
                while (output_length > 1 && output[output_length - 1] != '/') {
                    output_length--;
                }
                if (output_length > 1) output_length--;
                output[output_length] = '\0';
            }
            continue;
        }

        size_t needed = part_length + (output_length > 1 ? 1 : 0);
        if (output_length + needed + 1 > capacity) return 0;
        if (output_length > 1) output[output_length++] = '/';
        for (size_t i = 0; i < part_length; i++) {
            output[output_length++] = joined[start + i];
        }
        output[output_length] = '\0';
    }
    return 1;
}

int get_number(const value *item, int32_t *number)
{
    if (item->type != VALUE_NUMBER) return 0;
    *number = item->data.number;
    return 1;
}

value make_number(int32_t number)
{
    value result = make_value(VALUE_NUMBER);
    result.data.number = number;
    return result;
}

int checked_number(int64_t number, int32_t *result)
{
    if (number < -2147483648LL || number > 2147483647LL) return 0;
    *result = (int32_t)number;
    return 1;
}

int equal_values(const value *left, const value *right)
{
    if (left->type != right->type) return 0;
    switch (left->type) {
    case VALUE_NIL:
        return 1;
    case VALUE_TEXT:
    case VALUE_PATH:
    case VALUE_FILE:
    case VALUE_ERROR:
        return string_equal(left->data.text, right->data.text);
    case VALUE_NUMBER:
        return left->data.number == right->data.number;
    case VALUE_BOOLEAN:
        return left->data.boolean == right->data.boolean;
    case VALUE_FUNCTION:
        return left->data.function == right->data.function;
    case VALUE_LAMBDA:
        return left->data.lambda.body == right->data.lambda.body &&
               left->data.lambda.body_length ==
                   right->data.lambda.body_length;
    case VALUE_LIST:
        return left->data.list.items == right->data.list.items &&
               left->data.list.count == right->data.list.count;
    }
    return 0;
}
