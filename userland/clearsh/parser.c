#include "clearsh_internal.h"

typedef struct {
    const char *cursor;
} parser;

typedef struct {
    const char *name;
    function_id id;
} function_binding;

static const function_binding functions[] = {
    { "file", FUNCTION_FILE },
    { "read", FUNCTION_READ },
    { "write", FUNCTION_WRITE },
    { "clear", FUNCTION_CLEAR },
    { "print", FUNCTION_PRINT },
    { "text", FUNCTION_TEXT },
    { "list", FUNCTION_LIST },
    { "map", FUNCTION_MAP },
    { "filter", FUNCTION_FILTER },
    { "run", FUNCTION_RUN },
    { "help", FUNCTION_HELP },
    { "ls", FUNCTION_LS },
    { "cd", FUNCTION_CD },
    { "pwd", FUNCTION_PWD },
    { "lambda", FUNCTION_LAMBDA },
    { "\xCE\xBB", FUNCTION_LAMBDA },
    { "edit", FUNCTION_EDIT },
    { "vi", FUNCTION_EDIT },
    { "install", FUNCTION_INSTALL },
    { "exit", FUNCTION_EXIT },
    { "shutdown", FUNCTION_SHUTDOWN },
    { "poweroff", FUNCTION_SHUTDOWN },
    { "syscall", FUNCTION_SYSCALL },
    { "uptime", FUNCTION_UPTIME },
    { "+", FUNCTION_ADD },
    { "-", FUNCTION_SUBTRACT },
    { "*", FUNCTION_MULTIPLY },
    { "/", FUNCTION_DIVIDE },
    { "%", FUNCTION_MODULO },
    { "abs", FUNCTION_ABS },
    { "min", FUNCTION_MIN },
    { "max", FUNCTION_MAX },
    { "pow", FUNCTION_POWER },
    { "=", FUNCTION_EQUAL },
    { "<", FUNCTION_LESS },
    { "<=", FUNCTION_LESS_EQUAL },
    { ">", FUNCTION_GREATER },
    { ">=", FUNCTION_GREATER_EQUAL },
    { "not", FUNCTION_NOT },
    { "and", FUNCTION_AND },
    { "or", FUNCTION_OR },
    { "if", FUNCTION_IF },
    { "length", FUNCTION_LENGTH },
    { "concat", FUNCTION_CONCAT }
};

static value lambda_arguments[MAX_DEPTH];
static char lambda_parameters[MAX_DEPTH][PATH_MAX];
static size_t lambda_depth;

static int skip_expression(parser *p, unsigned int depth);

int clearsh_function_candidates(char *buffer,
                                size_t capacity,
                                size_t *length)
{
    size_t used = 0;
    if (!buffer || !length) return 0;
    for (size_t i = 0; i < sizeof(functions) / sizeof(functions[0]); i++) {
        size_t name_length = string_length(functions[i].name);
        if (used + name_length + 1 >= capacity) return 0;
        for (size_t j = 0; j < name_length; j++) {
            buffer[used++] = functions[i].name[j];
        }
        buffer[used++] = '\n';
    }
    if (used >= capacity) return 0;
    buffer[used] = '\0';
    *length = used;
    return 1;
}

static size_t edit_distance(const char *left, const char *right)
{
    uint16_t older[PATH_MAX];
    uint16_t previous[PATH_MAX];
    uint16_t current[PATH_MAX];
    size_t left_length = string_length(left);
    size_t right_length = string_length(right);
    if (left_length >= PATH_MAX || right_length >= PATH_MAX) {
        return PATH_MAX;
    }

    for (size_t j = 0; j <= right_length; j++) {
        previous[j] = (uint16_t)j;
    }
    for (size_t i = 1; i <= left_length; i++) {
        current[0] = (uint16_t)i;
        for (size_t j = 1; j <= right_length; j++) {
            uint16_t substitution = (uint16_t)(
                previous[j - 1] + (left[i - 1] != right[j - 1])
            );
            uint16_t insertion = (uint16_t)(current[j - 1] + 1);
            uint16_t deletion = (uint16_t)(previous[j] + 1);
            uint16_t best = substitution;
            if (insertion < best) best = insertion;
            if (deletion < best) best = deletion;
            if (i > 1 && j > 1 &&
                left[i - 1] == right[j - 2] &&
                left[i - 2] == right[j - 1] &&
                older[j - 2] + 1 < best) {
                best = (uint16_t)(older[j - 2] + 1);
            }
            current[j] = best;
        }
        for (size_t j = 0; j <= right_length; j++) {
            older[j] = previous[j];
            previous[j] = current[j];
        }
    }
    return previous[right_length];
}

static value unknown_function_error(const char *name)
{
    const char *suggestion = 0;
    size_t best_distance = PATH_MAX;
    size_t name_length = string_length(name);
    size_t function_count = sizeof(functions) / sizeof(functions[0]);
    for (size_t i = 0; i < function_count; i++) {
        size_t distance = edit_distance(name, functions[i].name);
        if (distance < best_distance) {
            best_distance = distance;
            suggestion = functions[i].name;
        }
    }

    char message[TEXT_MAX];
    size_t used = 0;
    static const char prefix[] = "unknown command '";
    static const char middle[] = "'; did you mean '";
    static const char suffix[] = "'?";
    static const char no_suggestion_suffix[] = "'";
    size_t max_distance = name_length < 4 ? 1 : 2;
    int has_suggestion = suggestion && best_distance <= max_distance;
    const char *parts[5];
    size_t part_count = 0;
    parts[part_count++] = prefix;
    parts[part_count++] = name;
    if (has_suggestion) {
        parts[part_count++] = middle;
        parts[part_count++] = suggestion;
        parts[part_count++] = suffix;
    } else {
        parts[part_count++] = no_suggestion_suffix;
    }

    for (size_t part = 0; part < part_count; part++) {
        const char *text = parts[part];
        while (*text && used + 1 < sizeof(message)) {
            message[used++] = *text++;
        }
    }
    message[used] = '\0';
    return make_error(message);
}

static void skip_space(parser *p)
{
    while (*p->cursor == ' ' || *p->cursor == '\t' ||
           *p->cursor == '\r' || *p->cursor == '\n') {
        p->cursor++;
    }
}

static int parse_string(parser *p, value *result)
{
    size_t used = 0;
    p->cursor++;
    result->type = VALUE_TEXT;

    while (*p->cursor && *p->cursor != '"') {
        char c = *p->cursor++;
        if (c == '\\') {
            c = *p->cursor++;
            if (!c) return 0;
            if (c == 'n') c = '\n';
            else if (c == 't') c = '\t';
            else if (c != '"' && c != '\\') return 0;
        }
        if (used + 1 >= sizeof(result->data.text)) return 0;
        result->data.text[used++] = c;
    }
    if (*p->cursor != '"') return 0;
    p->cursor++;
    result->data.text[used] = '\0';
    return 1;
}

static int lambda_body_length(const char *source, size_t *length)
{
    if (*source == '(') {
        size_t depth = 0;
        int in_string = 0;
        int escaped = 0;
        for (size_t i = 0; source[i]; i++) {
            char c = source[i];
            if (in_string) {
                if (escaped) escaped = 0;
                else if (c == '\\') escaped = 1;
                else if (c == '"') in_string = 0;
                continue;
            }
            if (c == '"') in_string = 1;
            else if (c == '(') depth++;
            else if (c == ')') {
                if (depth == 0) return 0;
                depth--;
                if (depth == 0) {
                    *length = i + 1;
                    return 1;
                }
            }
        }
        return 0;
    }

    if (*source == '"') {
        int escaped = 0;
        for (size_t i = 1; source[i]; i++) {
            if (escaped) escaped = 0;
            else if (source[i] == '\\') escaped = 1;
            else if (source[i] == '"') {
                *length = i + 1;
                return 1;
            }
        }
        return 0;
    }

    size_t i = 0;
    while (source[i] && source[i] != ')' &&
           source[i] != ' ' && source[i] != '\t' &&
           source[i] != '\r' && source[i] != '\n') {
        i++;
    }
    if (i == 0) return 0;
    *length = i;
    return 1;
}

static value parse_lambda_expression(parser *p)
{
    skip_space(p);
    if (*p->cursor != '(') return make_error("lambda expects one parameter");
    p->cursor++;
    skip_space(p);

    char parameter[PATH_MAX];
    size_t parameter_length = 0;
    while ((*p->cursor >= 'a' && *p->cursor <= 'z') ||
           (*p->cursor >= 'A' && *p->cursor <= 'Z') ||
           (*p->cursor >= '0' && *p->cursor <= '9') ||
           *p->cursor == '_') {
        if (parameter_length + 1 >= sizeof(parameter)) {
            return make_error("lambda parameter is too long");
        }
        parameter[parameter_length++] = *p->cursor++;
    }
    if (parameter_length == 0 ||
        !((parameter[0] >= 'a' && parameter[0] <= 'z') ||
          (parameter[0] >= 'A' && parameter[0] <= 'Z') ||
          parameter[0] == '_')) {
        return make_error("lambda expects one parameter name");
    }
    parameter[parameter_length] = '\0';

    skip_space(p);
    if (*p->cursor != ')') return make_error("invalid lambda parameter list");
    p->cursor++;
    skip_space(p);

    const char *body = p->cursor;
    size_t body_length = 0;
    if (!lambda_body_length(body, &body_length)) {
        return make_error("lambda expects one body expression");
    }
    p->cursor += body_length;
    skip_space(p);
    if (*p->cursor != ')') return make_error("missing lambda closing parenthesis");
    p->cursor++;

    value result = make_value(VALUE_LAMBDA);
    string_copy(result.data.lambda.parameter, parameter,
                sizeof(result.data.lambda.parameter));
    result.data.lambda.body = body;
    result.data.lambda.body_length = body_length;
    return result;
}

static value parse_atom(parser *p)
{
    char token[PATH_MAX];
    size_t used = 0;

    if (*p->cursor == '"') {
        value result = make_value(VALUE_TEXT);
        if (!parse_string(p, &result)) {
            return make_error("invalid or oversized string");
        }
        return result;
    }

    while (*p->cursor && *p->cursor != ')' &&
           *p->cursor != ' ' && *p->cursor != '\t' &&
           *p->cursor != '\r' && *p->cursor != '\n') {
        if (used + 1 >= sizeof(token)) {
            return make_error("token is too long");
        }
        token[used++] = *p->cursor++;
    }
    token[used] = '\0';
    if (used == 0) return make_error("expected a value");

    for (size_t binding = lambda_depth; binding > 0; binding--) {
        size_t index = binding - 1;
        if (string_equal(token, lambda_parameters[index])) {
            return lambda_arguments[index];
        }
    }

    if (string_equal(token, "nil")) return make_value(VALUE_NIL);
    if (string_equal(token, "true") || string_equal(token, "false")) {
        value result = make_value(VALUE_BOOLEAN);
        result.data.boolean = string_equal(token, "true");
        return result;
    }

    if (string_equal(token, "/")) {
        value result = make_value(VALUE_PATH);
        string_copy(result.data.text, token, sizeof(result.data.text));
        return result;
    }

    for (size_t i = 0; i < sizeof(functions) / sizeof(functions[0]); i++) {
        if (string_equal(token, functions[i].name)) {
            value result = make_value(VALUE_FUNCTION);
            result.data.function = functions[i].id;
            return result;
        }
    }

    size_t i = 0;
    int negative = 0;
    if (token[i] == '-') {
        negative = 1;
        i++;
    }
    if (token[i]) {
        int is_number = 1;
        size_t digits = i;
        for (; token[i]; i++) {
            if (token[i] < '0' || token[i] > '9') {
                is_number = 0;
                break;
            }
        }
        if (is_number) {
            uint32_t magnitude = 0;
            uint32_t limit = negative ? 0x80000000u : 0x7FFFFFFFu;
            for (size_t digit = digits; token[digit]; digit++) {
                uint32_t value = (uint32_t)(token[digit] - '0');
                if (magnitude > (limit - value) / 10u) {
                    return make_error("integer is out of range");
                }
                magnitude = magnitude * 10u + value;
            }
            value result = make_value(VALUE_NUMBER);
            if (negative) {
                result.data.number = magnitude == 0x80000000u
                    ? (-2147483647 - 1)
                    : -(int32_t)magnitude;
            } else {
                result.data.number = (int32_t)magnitude;
            }
            return result;
        }
    }

    value result = make_value(VALUE_PATH);
    string_copy(result.data.text, token, sizeof(result.data.text));
    return result;
}

static value parse_expression(parser *p, unsigned int depth)
{
    if (depth > MAX_DEPTH) return make_error("expression is too deep");
    skip_space(p);

    if (*p->cursor != '(') {
        return parse_atom(p);
    }
    p->cursor++;

    value function = parse_expression(p, depth + 1);
    if (function.type == VALUE_PATH &&
        string_equal(function.data.text, "/")) {
        function = make_value(VALUE_FUNCTION);
        function.data.function = FUNCTION_DIVIDE;
    }
    if (function.type != VALUE_FUNCTION) {
        if (function.type == VALUE_PATH) {
            return unknown_function_error(function.data.text);
        }
        return make_error("expression must start with a built-in function");
    }
    if (function.data.function == FUNCTION_LAMBDA) {
        return parse_lambda_expression(p);
    }

    if (function.data.function == FUNCTION_IF) {
        value condition = parse_expression(p, depth + 1);
        if (condition.type == VALUE_ERROR) return condition;
        skip_space(p);
        value selected;
        if (truthy(&condition)) {
            selected = parse_expression(p, depth + 1);
            if (selected.type == VALUE_ERROR) return selected;
            if (!skip_expression(p, depth + 1)) {
                return make_error("if expects condition and two branches");
            }
        } else {
            if (!skip_expression(p, depth + 1)) {
                return make_error("if expects condition and two branches");
            }
            selected = parse_expression(p, depth + 1);
            if (selected.type == VALUE_ERROR) return selected;
        }
        skip_space(p);
        if (*p->cursor != ')') {
            return make_error("if expects exactly three arguments");
        }
        p->cursor++;
        return selected;
    }

    value args[MAX_ARGS];
    size_t count = 0;
    skip_space(p);
    while (*p->cursor && *p->cursor != ')') {
        if (count == MAX_ARGS) return make_error("too many arguments");
        args[count++] = parse_expression(p, depth + 1);
        if (args[count - 1].type == VALUE_ERROR) return args[count - 1];
        skip_space(p);
    }
    if (*p->cursor != ')') return make_error("missing closing parenthesis");
    p->cursor++;
    return call_function(function.data.function, args, count);
}

value evaluate_lambda(const value *lambda, const value *argument)
{
    if (lambda_depth == MAX_DEPTH) {
        return make_error("lambda nesting is too deep");
    }

    size_t binding = lambda_depth++;
    string_copy(lambda_parameters[binding],
                lambda->data.lambda.parameter,
                sizeof(lambda_parameters[binding]));
    lambda_arguments[binding] = *argument;

    parser p = { lambda->data.lambda.body };
    value result = parse_expression(&p, 0);
    const char *end = lambda->data.lambda.body +
                      lambda->data.lambda.body_length;
    while (p.cursor < end &&
           (*p.cursor == ' ' || *p.cursor == '\t' ||
            *p.cursor == '\r' || *p.cursor == '\n')) {
        p.cursor++;
    }
    if (result.type != VALUE_ERROR && p.cursor != end) {
        result = make_error("invalid lambda body");
    }

    lambda_depth--;
    return result;
}

static int skip_expression(parser *p, unsigned int depth)
{
    if (depth > MAX_DEPTH) return 0;
    skip_space(p);

    if (*p->cursor == '(') {
        p->cursor++;
        skip_space(p);
        while (*p->cursor && *p->cursor != ')') {
            if (!skip_expression(p, depth + 1)) return 0;
            skip_space(p);
        }
        if (*p->cursor != ')') return 0;
        p->cursor++;
        return 1;
    }

    if (*p->cursor == '"') {
        p->cursor++;
        while (*p->cursor) {
            if (*p->cursor == '\\') {
                if (!p->cursor[1]) return 0;
                p->cursor += 2;
            } else if (*p->cursor++ == '"') {
                return 1;
            }
        }
        return 0;
    }

    const char *start = p->cursor;
    while (*p->cursor && *p->cursor != ')' &&
           *p->cursor != ' ' && *p->cursor != '\t' &&
           *p->cursor != '\r' && *p->cursor != '\n') {
        p->cursor++;
    }
    return p->cursor != start;
}

value evaluate_line(const char *line)
{
    parser p = { line };
    skip_space(&p);
    if (!*p.cursor || *p.cursor == ';') return make_value(VALUE_NIL);

    value result = parse_expression(&p, 0);
    if (result.type == VALUE_ERROR) return result;
    skip_space(&p);
    if (*p.cursor) return make_error("unexpected input after expression");
    return result;
}
