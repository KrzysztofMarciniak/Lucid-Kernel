# Userland Pioneer

The kernel's init bootstrap loads and enters the embedded Pioneer user image.
Pioneer forks, execs `/system/bin/clearsh`, and waits for it to exit; when
Clearsh exits, Pioneer starts it again. The current kernel has no preemptive
scheduler: `wait` switches to the child synchronously and resumes Pioneer when
the child exits. `exec` resolves paths from the kernel's embedded user-image
registry, not from RAMFS.

Before showing the prompt, Clearsh reads `/system/config/pioneer/boot` and runs
each listed script in order.

Each nonblank line in `boot` is a script path; relative paths are resolved
inside `/system/config/pioneer/`, absolute paths are accepted, and `#` starts a
comment. The default list is empty so startup reaches the prompt without
running informational scripts. The list and scripts can be edited from
Clearsh, and changes persist when booted from a disk image with the Lucid data
partition.

This is a single-child process model, not general multitasking. The recipe
list in `config.mk` remains the place for kernel-level initialization;
`/system/config/pioneer/boot` configures only userspace startup scripts.
