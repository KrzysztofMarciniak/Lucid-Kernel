# Userland Pioneer

The kernel's init bootstrap loads and enters the embedded Pioneer user image.
Pioneer reads `/system/config/defaults/window_manager`; Brain reads
`/system/config/defaults/terminal` when opening a terminal; Eye reads
`/system/config/defaults/shell` and execs the selected shell. These paths must
name images in the kernel's embedded user-image registry, not RAMFS files.
The defaults currently select Clearsh as shell, Eye as terminal, and Brain as
window manager.

The current kernel has no preemptive scheduler: `wait` switches to the child
synchronously and resumes Pioneer when the child exits.

Before showing the prompt, Clearsh reads `/system/config/pioneer/boot` and runs
each listed script in order.

Each nonblank line in `boot` is a script path; relative paths are resolved
inside `/system/config/pioneer/`, absolute paths are accepted, and `#` starts a
comment. The default list is empty so startup reaches the prompt without
running informational scripts. The list and scripts can be edited from
Clearsh, and changes persist when booted from a disk image with the Lucid data
partition.

Changes to the window-manager default apply on the next boot. Terminal changes
apply the next time Brain opens a terminal; shell changes apply the next time
Eye starts. Brain shows the log view in window 1 on desktop 1 only, then
switches to desktop 2 and opens the startup terminal there. Terminal windows
start at 2. This is a single-child process model, not general multitasking. The
recipe list in `config.mk` remains the place for kernel-level initialization;
`/system/config/pioneer/boot` configures only userspace startup scripts.
