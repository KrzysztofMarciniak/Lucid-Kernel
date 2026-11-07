#include "panic.h"
#include "recipes/gdt.h"
#include "recipes/ramfs.h"
#include "recipes/syscall.h"
#include "recipes/terminal.h"
#include "recipes/usermode.h"
#include "recipes/vmm.h"
#include "userland_image.h"

#define PROCESS_LIMIT (USERMODE_PROCESS_SLOTS - 1)
#define PROCESS_KERNEL_STACK_SIZE 4096
#define PROCESS_INPUT_QUEUE_SIZE 64
#define DESKTOP_COUNT 10

typedef enum {
    PROCESS_UNUSED,
    PROCESS_READY,
    PROCESS_RUNNING,
    PROCESS_WAITING,
    PROCESS_EXITED
} process_state;

typedef struct process process;

struct process {
    int in_use;
    uint32_t pid;
    uint32_t exit_status;
    uint32_t wait_pid;
    process_state state;
    int waiting_input;
    int reap_on_switch;
    int is_window;
    int is_manager;
    uint32_t desktop;
    process *parent;
    const char *name;
    vmm_space *space;
    usermode_frame frame;
    uint32_t *saved_stack;
    uint32_t kernel_stack_top;
    uint32_t return_esp;
    uint8_t kernel_stack[PROCESS_KERNEL_STACK_SIZE]
        __attribute__((aligned(16)));
};

static process process_table[PROCESS_LIMIT];
static process root_process;
static process *current_process;
static uint32_t next_pid = 2;
static uint32_t input_queue[USERMODE_PROCESS_SLOTS][PROCESS_INPUT_QUEUE_SIZE];
static uint32_t input_queue_head[USERMODE_PROCESS_SLOTS];
static uint32_t input_queue_tail[USERMODE_PROCESS_SLOTS];
static uint32_t focused_slot;
static uint32_t window_manager_slot;
static uint32_t current_desktop = 1;
static uint32_t master_ratio = 60;
static int manager_process_slot_reserved;

static process *process_at_slot(uint32_t slot);

static process *current(void)
{
    if (current_process) return current_process;

    root_process.in_use = 1;
    root_process.pid = 1;
    root_process.state = PROCESS_RUNNING;
    root_process.name = userland_initial_image()->path;
    root_process.space = vmm_space_current();
    root_process.kernel_stack_top = usermode_kernel_stack_top();
    root_process.return_esp = usermode_return_esp();
    current_process = &root_process;
    return current_process;
}

static int is_configured_program(const char *path, const char *config_path)
{
    char configured_path[SYS_PATH_MAX + 1];
    size_t length = 0;
    if (ramfs_read(config_path, configured_path,
                   sizeof(configured_path), &length) != 0 ||
        length >= sizeof(configured_path)) {
        return 0;
    }
    while (length &&
           (configured_path[length - 1] == '\n' ||
            configured_path[length - 1] == '\r')) {
        length--;
    }
    if (length == 0 || length >= SYS_PATH_MAX) return 0;
    configured_path[length] = '\0';

    size_t i = 0;
    while (i < length && path[i] && path[i] == configured_path[i]) i++;
    return i == length && path[i] == '\0';
}

uint32_t usermode_process_fork(usermode_frame *frame)
{
    process *parent = current();
    process *child = 0;
    uint32_t first_slot = manager_process_slot_reserved ? 1 : 0;
    for (uint32_t i = first_slot; i < PROCESS_LIMIT; i++) {
        if (!process_table[i].in_use) {
            child = &process_table[i];
            break;
        }
    }
    if (!child) return SYS_UNKNOWN_TEST;
    if (child == &process_table[0]) manager_process_slot_reserved = 1;

    vmm_space *child_space = 0;
    lucid_status st = vmm_space_clone_current(&child_space);
    if (st.ok != 0) return SYS_UNKNOWN_TEST;

    child->in_use = 1;
    child->pid = next_pid++;
    if (child->pid == 0 || child->pid == SYS_UNKNOWN_TEST) {
        child->pid = next_pid++;
    }
    child->exit_status = 0;
    child->wait_pid = 0;
    child->state = PROCESS_READY;
    child->waiting_input = 0;
    child->reap_on_switch = 0;
    child->is_window = 0;
    child->is_manager = 0;
    child->parent = parent;
    child->name = parent->name;
    child->space = child_space;
    child->frame = *frame;
    child->frame.eax = 0;
    child->kernel_stack_top =
        (uint32_t)(child->kernel_stack + sizeof(child->kernel_stack));
    child->return_esp = usermode_return_esp();
    child->saved_stack = (uint32_t *)(
        child->kernel_stack_top - sizeof(child->frame)
    );
    uint32_t child_slot = (uint32_t)(child - process_table) + 1;
    input_queue_head[child_slot] = 0;
    input_queue_tail[child_slot] = 0;
    *child->saved_stack = (uint32_t)child->frame.gs;
    for (uint32_t i = 1; i < sizeof(child->frame) / sizeof(uint32_t); i++) {
        child->saved_stack[i] = ((uint32_t *)&child->frame)[i];
    }
    return child->pid;
}

static void release_process(process *entry)
{
    if (!entry || !entry->in_use || entry == &root_process) return;
    lucid_status st = vmm_space_destroy(entry->space);
    if (st.ok != 0) panic_status(st);
    entry->in_use = 0;
    entry->state = PROCESS_UNUSED;
    entry->parent = 0;
    entry->space = 0;
    entry->saved_stack = 0;
    entry->wait_pid = 0;
    entry->waiting_input = 0;
    entry->reap_on_switch = 0;
    entry->is_window = 0;
    uint32_t slot = (uint32_t)(entry - process_table) + 1;
    if (window_manager_slot == slot) window_manager_slot = 0;
    entry->is_manager = 0;
    input_queue_head[slot] = 0;
    input_queue_tail[slot] = 0;
}

static void enqueue_key(uint32_t slot, uint32_t key)
{
    uint32_t next = (input_queue_tail[slot] + 1) %
        PROCESS_INPUT_QUEUE_SIZE;
    if (next == input_queue_head[slot]) return;
    input_queue[slot][input_queue_tail[slot]] = key;
    input_queue_tail[slot] = next;
}

int usermode_process_read_key(void)
{
    uint32_t slot = usermode_process_current_slot();
    if (input_queue_head[slot] == input_queue_tail[slot]) return -1;
    uint32_t key = input_queue[slot][input_queue_head[slot]];
    input_queue_head[slot] = (input_queue_head[slot] + 1) %
        PROCESS_INPUT_QUEUE_SIZE;
    return (int)key;
}

static int process_is_visible_window(uint32_t slot)
{
    process *entry = process_at_slot(slot);
    return entry && entry->in_use && entry->is_window &&
        entry->state != PROCESS_EXITED &&
        entry->desktop == current_desktop;
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

static void close_window(process *entry)
{
    if (!entry || !entry->in_use || !entry->is_window ||
        entry->state == PROCESS_EXITED) {
        return;
    }
    entry->state = PROCESS_EXITED;
    entry->exit_status = 0;
    terminal_set_status("Killing terminal");
    enqueue_key(window_manager_slot, SYS_WM_REAP);
    if (focused_slot == (uint32_t)(entry == &root_process ? 0 :
            (entry - process_table + 1))) {
        focused_slot = next_window_slot(focused_slot, 1);
    }
}

void usermode_process_route_key(int key)
{
    if (key >= TERMINAL_KEY_MOD_BASE &&
        key < TERMINAL_KEY_MOD_SHIFT_BASE) {
        int command = key - TERMINAL_KEY_MOD_BASE;
        if (window_manager_slot == 0) return;
        if (command == '\n') {
            terminal_set_status("Starting terminal");
            enqueue_key(window_manager_slot, SYS_WM_OPEN);
        } else if (command == 'q') {
            process *focused = process_at_slot(focused_slot);
            close_window(focused);
        } else if (command == 'j') {
            focused_slot = next_window_slot(focused_slot, 1);
            terminal_set_status("Focus changed");
        } else if (command == 'k') {
            focused_slot = next_window_slot(focused_slot, -1);
            terminal_set_status("Focus changed");
        } else if (command >= '0' && command <= '9') {
            current_desktop = (uint32_t)(command - '0');
            focused_slot = next_window_slot(USERMODE_PROCESS_SLOTS - 1, 1);
            terminal_set_status("Desktop changed");
        }
        terminal_refresh();
        return;
    }
    if (key >= TERMINAL_KEY_MOD_SHIFT_BASE &&
        key < TERMINAL_KEY_MOD_SHIFT_BASE + 0x100) {
        int command = key - TERMINAL_KEY_MOD_SHIFT_BASE;
        if (window_manager_slot == 0) return;
        if (command >= 'A' && command <= 'Z') {
            command += 'a' - 'A';
        }
        if (command == 'q') {
            terminal_set_status("Brain exiting");
            for (uint32_t slot = 1; slot < USERMODE_PROCESS_SLOTS; slot++) {
                close_window(process_at_slot(slot));
            }
            terminal_set_status("Brain exiting");
            enqueue_key(window_manager_slot, SYS_WM_EXIT);
        } else if (command == 'h' && master_ratio > 30) {
            master_ratio -= 5;
            terminal_set_status("Master resized");
        } else if (command == 'l' && master_ratio < 80) {
            master_ratio += 5;
            terminal_set_status("Master resized");
        } else if (command >= '0' && command <= '9') {
            process *focused = process_at_slot(focused_slot);
            if (focused && focused->in_use && focused->is_window) {
                focused->desktop = (uint32_t)(command - '0');
                if (focused->desktop != current_desktop) {
                    focused_slot = next_window_slot(focused_slot, 1);
                }
                terminal_set_status("Window moved");
            }
        }
        terminal_refresh();
        return;
    }

    uint32_t slot = focused_slot;
    if (!process_is_visible_window(slot)) slot = 0;
    enqueue_key(slot, (uint32_t)key);
}

uint32_t usermode_process_reap(void)
{
    process *parent = current();
    uint32_t reaped = 0;
    for (uint32_t i = 0; i < PROCESS_LIMIT; i++) {
        process *child = &process_table[i];
        if (child->in_use && child->parent == parent &&
            child->state == PROCESS_EXITED) {
            release_process(child);
            reaped++;
        }
    }
    return reaped;
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

uint32_t usermode_process_window_pid(uint32_t slot)
{
    process *entry = process_at_slot(slot);
    return entry && entry->in_use && entry->is_window ? entry->pid : 0;
}

uint32_t usermode_process_current_desktop(void)
{
    return current_desktop;
}

uint32_t usermode_process_window_desktop(uint32_t slot)
{
    process *entry = process_at_slot(slot);
    return entry && entry->in_use && entry->is_window
        ? entry->desktop
        : current_desktop;
}

uint32_t usermode_process_master_ratio(void)
{
    return master_ratio;
}

int usermode_process_wait(uint32_t pid, usermode_frame *frame)
{
    process *parent = current();
    for (uint32_t i = 0; i < PROCESS_LIMIT; i++) {
        process *child = &process_table[i];
        if (!child->in_use || child->parent != parent ||
            child->pid != pid) {
            continue;
        }

        if (child->state == PROCESS_EXITED) {
            frame->eax = child->pid;
            release_process(child);
            return 0;
        }

        parent->state = PROCESS_WAITING;
        parent->wait_pid = pid;
        parent->saved_stack = (uint32_t *)frame;
        return 1;
    }
    frame->eax = SYS_UNKNOWN_TEST;
    return 0;
}

static process *process_at_slot(uint32_t slot)
{
    if (slot == 0) return &root_process;
    if (slot > PROCESS_LIMIT) return 0;
    return &process_table[slot - 1];
}

static process *next_runnable(process *from)
{
    uint32_t start = from == &root_process ? 0 : PROCESS_LIMIT + 1;
    if (from != &root_process) {
        for (uint32_t i = 0; i < PROCESS_LIMIT; i++) {
            if (&process_table[i] == from) {
                start = i + 1;
                break;
            }
        }
    }

    for (uint32_t offset = 1; offset <= PROCESS_LIMIT + 1; offset++) {
        uint32_t slot = (start + offset) % (PROCESS_LIMIT + 1);
        process *candidate = process_at_slot(slot);
        if (candidate && candidate->in_use &&
            candidate->state == PROCESS_READY) {
            return candidate;
        }
    }
    if (from && from->in_use && from->state == PROCESS_READY) return from;
    return 0;
}

uint32_t *usermode_process_schedule(usermode_frame *frame)
{
    process *previous = current();
    previous->saved_stack = (uint32_t *)frame;
    if (previous->state == PROCESS_RUNNING) {
        previous->state = PROCESS_READY;
    }

    process *next = next_runnable(previous);
    if (!next) return 0;
    next->state = PROCESS_RUNNING;
    current_process = next;

    lucid_status st = vmm_space_activate(next->space);
    if (st.ok != 0) panic_status(st);
    usermode_set_kernel_stack_top(next->kernel_stack_top);

    if (previous->reap_on_switch) release_process(previous);
    return next->saved_stack;
}

uint32_t *usermode_process_schedule_interrupt(uint32_t *stack)
{
    usermode_frame *frame = (usermode_frame *)stack;
    process *active = current();
    if ((frame->cs & 3u) != 3u && !active->waiting_input) {
        return stack;
    }

    return usermode_process_schedule(frame);
}

void usermode_process_set_waiting_input(int waiting)
{
    process *active = current();
    active->waiting_input = waiting;
}

uint32_t usermode_process_current_slot(void)
{
    process *active = current_process;
    if (!active) return 0;
    if (active == &root_process) return 0;
    for (uint32_t i = 0; i < PROCESS_LIMIT; i++) {
        if (&process_table[i] == active) return i + 1;
    }
    return 0;
}

int usermode_process_exec(const char *path, usermode_frame *frame)
{
    process *process = current();
    const userland_image *image = userland_find_image(path);
    if (!image) {
        terminal_write("exec failed: image not registered\n");
        return 0;
    }
    static const char eye_path[] = "/system/bin/eye";
    static const char brain_path[] = "/system/bin/brain";
    static const char terminal_config[] =
        "/system/config/defaults/terminal";
    static const char manager_config[] =
        "/system/config/defaults/window_manager";
    int is_terminal = is_configured_program(path, terminal_config);
    int is_manager = is_configured_program(path, manager_config);
    if (!is_terminal) {
        is_terminal = 1;
        for (uint32_t i = 0; i < sizeof(eye_path); i++) {
            if (path[i] != eye_path[i]) is_terminal = 0;
        }
    }
    if (!is_manager) {
        is_manager = 1;
        for (uint32_t i = 0; i < sizeof(brain_path); i++) {
            if (path[i] != brain_path[i]) is_manager = 0;
        }
    }

    lucid_status st = vmm_space_clear_user(process->space);
    if (st.ok != 0) {
        terminal_write("exec failed: address space not active\n");
        return 0;
    }

    st = usermode_reset_stack();
    if (st.ok == 0) {
        st = usermode_load_image(
            image->load_address,
            image->start,
            userland_image_size(image),
            image->memory_size
        );
    }
    if (st.ok != 0) {
        usermode_process_exit(SYS_UNKNOWN_TEST);
        return -1;
    }

    frame->edi = 0;
    frame->esi = 0;
    frame->ebp = 0;
    frame->saved_esp = 0;
    frame->ebx = 0;
    frame->edx = 0;
    frame->ecx = 0;
    frame->eax = 0;
    frame->eip = image->load_address;
    frame->eflags = 0x202;
    frame->user_esp = USERMODE_STACK_TOP;
    process->name = image->path;
    uint32_t slot = usermode_process_current_slot();
    if (!(is_manager && slot == 1)) {
        terminal_process_reset(slot);
    }
    if (is_manager) {
        process->is_manager = 1;
        window_manager_slot = slot;
        current_desktop = 2;
        focused_slot = 0;
        terminal_config_reload();
    }
    if (is_terminal) {
        process->is_window = 1;
        process->desktop = current_desktop;
        focused_slot = slot;
        terminal_refresh();
    }
    return 1;
}

static int process_name_matches(const char *name, const char *image)
{
    if (!name || !image) return 0;
    const char *image_base = image;
    for (const char *cursor = image; *cursor; cursor++) {
        if (*cursor == '/') image_base = cursor + 1;
    }

    const char *left = name;
    const char *right = image;
    while (*left && *right && *left == *right) {
        left++;
        right++;
    }
    if (!*left && !*right) return 1;

    left = name;
    right = image_base;
    while (*left && *right && *left == *right) {
        left++;
        right++;
    }
    return !*left && !*right;
}

static int process_mark_exited(process *exiting, uint32_t status)
{
    exiting->exit_status = status;
    exiting->state = PROCESS_EXITED;
    if (exiting->is_window) {
        uint32_t slot = exiting == &root_process
            ? 0
            : (uint32_t)(exiting - process_table) + 1;
        if (focused_slot == slot) {
            focused_slot = next_window_slot(slot, 1);
        }
        enqueue_key(window_manager_slot, SYS_WM_REAP);
        terminal_refresh();
    }
    if (exiting->parent &&
        exiting->parent->state == PROCESS_WAITING &&
        exiting->parent->wait_pid == exiting->pid) {
        usermode_frame *waiting_frame =
            (usermode_frame *)exiting->parent->saved_stack;
        waiting_frame->eax = exiting->pid;
        exiting->parent->wait_pid = 0;
        exiting->parent->state = PROCESS_READY;
        exiting->reap_on_switch = 1;
        return 1;
    }
    return 0;
}

void usermode_process_exit(uint32_t status)
{
    process_mark_exited(current(), status);
}

int usermode_process_kill(const char *name)
{
    if (!name || !*name) return 0;

    process *match = 0;
    for (uint32_t i = 0; i < PROCESS_LIMIT; i++) {
        process *entry = &process_table[i];
        if (!entry->in_use || entry->state == PROCESS_EXITED ||
            entry == current_process || !entry->name ||
            !process_name_matches(name, entry->name)) {
            continue;
        }
        if (match) return -1;
        match = entry;
    }
    if (!match) return 0;

    if (process_mark_exited(match, SYS_UNKNOWN_TEST)) {
        release_process(match);
    }
    return 1;
}

static int append_process_text(char *buffer,
                               size_t capacity,
                               size_t *used,
                               const char *text)
{
    while (*text) {
        if (*used == capacity) return 0;
        buffer[(*used)++] = *text++;
    }
    return 1;
}

static int append_process_number(char *buffer,
                                 size_t capacity,
                                 size_t *used,
                                 uint32_t number)
{
    char digits[10];
    size_t count = 0;
    do {
        digits[count++] = (char)('0' + number % 10);
        number /= 10;
    } while (number && count < sizeof(digits));
    while (count) {
        if (*used == capacity) return 0;
        buffer[(*used)++] = digits[--count];
    }
    return 1;
}

static const char *process_state_name(process_state state)
{
    if (state == PROCESS_READY) return "ready";
    if (state == PROCESS_RUNNING) return "running";
    if (state == PROCESS_WAITING) return "waiting";
    if (state == PROCESS_EXITED) return "exited";
    return "unused";
}

static int append_process(char *buffer,
                          size_t capacity,
                          size_t *used,
                          const process *entry)
{
    uint32_t parent_pid = entry->parent ? entry->parent->pid : 0;
    return append_process_number(buffer, capacity, used, entry->pid) &&
           append_process_text(buffer, capacity, used, " ") &&
           append_process_number(buffer, capacity, used, parent_pid) &&
           append_process_text(buffer, capacity, used, " ") &&
           append_process_text(
               buffer, capacity, used, process_state_name(entry->state)
           ) &&
           append_process_text(buffer, capacity, used, " ") &&
           append_process_text(
               buffer, capacity, used, entry->name ? entry->name : "-"
           ) &&
           append_process_text(buffer, capacity, used, "\n");
}

int usermode_process_info(char *buffer, size_t capacity, size_t *length)
{
    if (!buffer || !length) return 1;
    size_t used = 0;
    if (!append_process_text(buffer, capacity, &used,
                             "PID PPID STATE IMAGE\n")) {
        return 1;
    }

    if (root_process.in_use &&
        !append_process(buffer, capacity, &used, &root_process)) {
        return 1;
    }
    for (uint32_t i = 0; i < PROCESS_LIMIT; i++) {
        if (process_table[i].in_use &&
            !append_process(buffer, capacity, &used, &process_table[i])) {
            return 1;
        }
    }
    *length = used;
    return 0;
}
