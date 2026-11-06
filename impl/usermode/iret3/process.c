#include "panic.h"
#include "recipes/gdt.h"
#include "recipes/syscall.h"
#include "recipes/usermode.h"
#include "recipes/vmm.h"
#include "userland_image.h"

#define PROCESS_LIMIT 8
#define PROCESS_KERNEL_STACK_SIZE 4096

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
    process_state state;
    process *parent;
    const char *name;
    vmm_space *space;
    usermode_frame frame;
    uint32_t kernel_stack_top;
    uint32_t return_esp;
    uint8_t kernel_stack[PROCESS_KERNEL_STACK_SIZE]
        __attribute__((aligned(16)));
};

static process process_table[PROCESS_LIMIT];
static process root_process;
static process *current_process;
static uint32_t next_pid = 2;

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

uint32_t usermode_process_fork(usermode_frame *frame)
{
    process *parent = current();
    process *child = 0;
    for (uint32_t i = 0; i < PROCESS_LIMIT; i++) {
        if (!process_table[i].in_use) {
            child = &process_table[i];
            break;
        }
    }
    if (!child) return SYS_UNKNOWN_TEST;

    vmm_space *child_space = 0;
    lucid_status st = vmm_space_clone_current(&child_space);
    if (st.ok != 0) return SYS_UNKNOWN_TEST;

    child->in_use = 1;
    child->pid = next_pid++;
    if (child->pid == 0 || child->pid == SYS_UNKNOWN_TEST) {
        child->pid = next_pid++;
    }
    child->exit_status = 0;
    child->state = PROCESS_READY;
    child->parent = parent;
    child->name = parent->name;
    child->space = child_space;
    child->frame = *frame;
    child->frame.eax = 0;
    child->kernel_stack_top =
        (uint32_t)(child->kernel_stack + sizeof(child->kernel_stack));
    child->return_esp = usermode_return_esp();
    return child->pid;
}

uint32_t usermode_process_wait(uint32_t pid)
{
    process *parent = current();
    process *child = 0;
    for (uint32_t i = 0; i < PROCESS_LIMIT; i++) {
        if (process_table[i].in_use &&
            process_table[i].parent == parent &&
            process_table[i].pid == pid) {
            child = &process_table[i];
            break;
        }
    }
    if (!child || child->state != PROCESS_READY) return SYS_UNKNOWN_TEST;

    parent->space = vmm_space_current();
    parent->kernel_stack_top = usermode_kernel_stack_top();
    parent->return_esp = usermode_return_esp();
    parent->state = PROCESS_WAITING;
    child->state = PROCESS_RUNNING;
    current_process = child;

    lucid_status st = vmm_space_activate(child->space);
    if (st.ok != 0) {
        current_process = parent;
        parent->state = PROCESS_RUNNING;
        child->state = PROCESS_READY;
        return SYS_UNKNOWN_TEST;
    }
    usermode_set_kernel_stack_top(child->kernel_stack_top);
    usermode_resume_frame(&child->frame);

    current_process = parent;
    parent->state = PROCESS_RUNNING;
    st = vmm_space_activate(parent->space);
    if (st.ok != 0) panic_status(st);
    usermode_set_kernel_stack_top(parent->kernel_stack_top);
    usermode_set_return_esp(parent->return_esp);

    uint32_t child_pid = child->pid;
    st = vmm_space_destroy(child->space);
    if (st.ok != 0) panic_status(st);
    child->in_use = 0;
    child->state = PROCESS_UNUSED;
    child->parent = 0;
    child->space = 0;
    return child_pid;
}

int usermode_process_exec(const char *path, usermode_frame *frame)
{
    process *process = current();
    const userland_image *image = userland_find_image(path);
    if (!image) return 0;

    lucid_status st = vmm_space_clear_user(process->space);
    if (st.ok != 0) return 0;

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
        process->exit_status = SYS_UNKNOWN_TEST;
        process->state = PROCESS_EXITED;
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
    return 1;
}

void usermode_process_exit(uint32_t status)
{
    process *process = current_process;
    if (!process) return;
    process->exit_status = status;
    process->state = PROCESS_EXITED;
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
