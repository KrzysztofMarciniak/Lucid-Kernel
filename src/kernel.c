/* kernel.c - fixed core. Never swapped, no config.mk knob.
 *
 * The only piece of code allowed to know about every recipe by name,
 * since it's what drives the three-pass boot: all _pre, then all
 * _init, then all _post, in INIT_BOOT order within each pass.
 */
#include "init_table.h"
#include "panic.h"
#include "recipes/terminal.h"

static void banner(void)
{
    terminal_write("LUCID KERNEL\n");
    terminal_write("\n");
}

static void phase(const char *name)
{
    terminal_write("[");
    terminal_write(name);
    terminal_write("]\n");
}

static void report(const char *recipe,
                   const char *pass,
                   lucid_status st)
{
    terminal_write("  [OK] ");
    terminal_write(recipe);
    terminal_write(".");
    terminal_write(pass);

    if (st.msg) {
        terminal_write(" - ");
        terminal_write(st.msg);
    }

    terminal_write("\n");
}

void kernel_main(void)
{
    unsigned int i;

    banner();

    phase("PRE");
    for (i = 0; i < init_table_count; i++) {
        lucid_status st = init_table[i].pre();
        if (st.ok)
            panic_status(st);
        report(init_table[i].name, "pre", st);
    }

    terminal_write("\n");

    phase("INIT");
    for (i = 0; i < init_table_count; i++) {
        lucid_status st = init_table[i].init();
        if (st.ok)
            panic_status(st);
        report(init_table[i].name, "init", st);
    }

    terminal_write("\n");

    phase("POST");
    for (i = 0; i < init_table_count; i++) {
        lucid_status st = init_table[i].post();
        if (st.ok)
            panic_status(st);
        report(init_table[i].name, "post", st);
    }

    terminal_write("\n");
    terminal_write("Boot complete.\n");

    panic_ok("lucid: boot complete, all recipes up, halting.");

    for (;;) {
        __asm__ volatile ("hlt");
    }
}
