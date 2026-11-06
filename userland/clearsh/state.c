#include "clearsh_internal.h"

value arena[ARENA_SIZE];
size_t arena_used;
char current_directory[PATH_MAX] = "/users/root";
char pending_script_path[PATH_MAX];
int script_pending;
