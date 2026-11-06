#include "recipes/init.h"
#include "recipes/terminal.h"
#include "recipes/usermode.h"
#include "userland_image.h"

static const userland_image *initial_image;

lucid_status init_pre(void)
{
    return LUCID_OK;
}

lucid_status init_init(void)
{
    return LUCID_OK;
}

lucid_status init_post(void)
{
    initial_image = userland_initial_image();
    if (!initial_image) {
        return LUCID_FAIL_EX(
            1,
            "Userland bootstrap image missing",
            "no initial image is registered",
            "add a bootable image to the userland image registry"
        );
    }

    return usermode_load_image(
        initial_image->load_address,
        initial_image->start,
        userland_image_size(initial_image),
        initial_image->memory_size
    );
}

void init_run(void)
{
    terminal_write("Kernel: starting userland Pioneer\n");
    usermode_enter((void *)initial_image->load_address);
    terminal_write("Userland Pioneer exited.\n");
    for (;;) __asm__ volatile ("hlt");
}
