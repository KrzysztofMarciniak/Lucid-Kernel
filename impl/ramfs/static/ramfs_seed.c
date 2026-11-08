#include "ramfs_internal.h"

const ramfs_seed_entry ramfs_seeds[] = {
    { "/", 0, 0, 1 },
    { "/system", 0, 0, 1 },
    { "/system/info", 0, 0, 1 },
    { "/system/config", 0, 0, 1 },
    { "/system/config/defaults", 0, 0, 1 },
    { "/system/config/clearsh", 0, 0, 1 },
    { "/system/config/pioneer", 0, 0, 1 },
    { "/system/config/brain", 0, 0, 1 },
    { "/system/config/defaults/shell", "/system/bin/clearsh\n",
      sizeof("/system/bin/clearsh\n") - 1, 0 },
    { "/system/config/defaults/window_manager",
      "/system/bin/brain\n", sizeof("/system/bin/brain\n") - 1, 0 },
    { "/system/config/defaults/terminal",
      "/system/bin/eye\n", sizeof("/system/bin/eye\n") - 1, 0 },
    { "/system/config/clearsh/zoom", "1\n", 2, 0 },
    { "/system/config/clearsh/prompt",
      "\xCE\xBB %p> \n", sizeof("\xCE\xBB %p> \n") - 1, 0 },
    { "/system/config/resolution", "1024x768x32\n", 12, 0 },
    { "/system/config/brain.conf", "bar=top\n", 8, 0 },
    { "/system/config/brain/layout",
      "desktops=10\n"
      "startup_desktop=2\n"
      "log_desktop=1\n"
      "master_ratio=60\n"
      "master_min_ratio=30\n"
      "master_max_ratio=80\n"
      "master_step=5\n",
      sizeof("desktops=10\n"
             "startup_desktop=2\n"
             "log_desktop=1\n"
             "master_ratio=60\n"
             "master_min_ratio=30\n"
             "master_max_ratio=80\n"
             "master_step=5\n") - 1, 0 },
    { "/system/config/brain/keys",
      "key_open=alt+enter\n"
      "key_close=alt+q\n"
      "key_focus_next=alt+j\n"
      "key_focus_previous=alt+k\n"
      "key_exit=alt+shift+q\n"
      "key_master_shrink=alt+shift+h\n"
      "key_master_grow=alt+shift+l\n"
      "key_switch_desktop=alt+digit\n"
      "key_move_desktop=alt+shift+digit\n",
      sizeof("key_open=alt+enter\n"
             "key_close=alt+q\n"
             "key_focus_next=alt+j\n"
             "key_focus_previous=alt+k\n"
             "key_exit=alt+shift+q\n"
             "key_master_shrink=alt+shift+h\n"
             "key_master_grow=alt+shift+l\n"
             "key_switch_desktop=alt+digit\n"
             "key_move_desktop=alt+shift+digit\n") - 1, 0 },
    { "/system/config/brain/theme",
      "background=000000\n"
      "foreground=FFFFFF\n"
      "accent=00FF00\n",
      sizeof("background=000000\n"
             "foreground=FFFFFF\n"
             "accent=00FF00\n") - 1, 0 },
    { "/system/config/brain/statusbar",
      "enabled=1\n"
      "interval_ms=5000\n"
      "script=/users/root/example-scripts/statusbar.csh\n"
      "output=/system/config/brain/statusbar_output\n",
      sizeof("enabled=1\n"
             "interval_ms=5000\n"
             "script=/users/root/example-scripts/statusbar.csh\n"
             "output=/system/config/brain/statusbar_output\n") - 1, 0 },
    { "/system/config/brain/statusbar_output", "", 0, 0 },
    { "/users", 0, 0, 1 },
    { "/users/root", 0, 0, 1 },
    { "/users/root/example-scripts", 0, 0, 1 },
    { "/tmp", 0, 0, 1 },
    { "/README.TXT",
      "Lucid Kernel\n"
      "============\n"
      "Lucid is a compact, non-POSIX hobby operating system.\n"
      "Boot flow: Kernel -> Pioneer -> Brain -> Eye -> Clearsh.\n"
      "You are at the Clearsh prompt. Try (help) or (ls (file /)).\n"
      "Example scripts are in /users/root/example-scripts.\n"
      "Run one with (run (file /users/root/example-scripts/hello.csh)).\n"
      "Read this guide with:\n"
      "  (print (read (file /README.TXT)))\n"
      "Other guides: /CLEARSH.TXT, /BRAIN.TXT, /SYSTEM.TXT\n"
      "Files in RAM are volatile unless booted with a Lucid disk image.\n",
      sizeof("Lucid Kernel\n"
             "============\n"
             "Lucid is a compact, non-POSIX hobby operating system.\n"
             "Boot flow: Kernel -> Pioneer -> Brain -> Eye -> Clearsh.\n"
             "You are at the Clearsh prompt. Try (help) or (ls (file /)).\n"
             "Example scripts are in /users/root/example-scripts.\n"
             "Run one with (run (file /users/root/example-scripts/hello.csh)).\n"
             "Read this guide with:\n"
             "  (print (read (file /README.TXT)))\n"
             "Other guides: /CLEARSH.TXT, /BRAIN.TXT, /SYSTEM.TXT\n"
             "Files in RAM are volatile unless booted with a Lucid disk image.\n") - 1,
      0 },
    { "/CLEARSH.TXT",
      "Clearsh guide\n"
      "=============\n"
      "Clearsh evaluates one Lisp expression per input line.\n"
      "  (ls (file /))\n"
      "  (print (read (file /system/info/name)))\n"
      "  (write (file /users/root/note) (text \"hello\\n\"))\n"
      "Use (pwd), (cd (file /path)), (vi (file /path)), (run (file /path)),\n"
      "(uptime), and (exit). Use (help) for more examples.\n"
      "Arrows edit; Up/Down browse history. Ctrl+Left/Right move by word;\n"
      "Ctrl+A/E go to line ends; Ctrl+U/K delete to an end; Ctrl+W deletes\n"
      "a word; Ctrl+L clears. Tab completes function names and file paths.\n"
      "On framebuffer displays, set zoom with:\n"
      "  (write (file /system/config/clearsh/zoom) (text \"2\"))\n"
      "Zoom values are 1-4; VGA text mode stays at native size.\n",
      sizeof("Clearsh guide\n"
             "=============\n"
             "Clearsh evaluates one Lisp expression per input line.\n"
             "  (ls (file /))\n"
             "  (print (read (file /system/info/name)))\n"
             "  (write (file /users/root/note) (text \"hello\\n\"))\n"
             "Use (pwd), (cd (file /path)), (vi (file /path)), (run (file /path)),\n"
             "(uptime), and (exit). Use (help) for more examples.\n"
             "Arrows edit; Up/Down browse history. Ctrl+Left/Right move by word;\n"
             "Ctrl+A/E go to line ends; Ctrl+U/K delete to an end; Ctrl+W deletes\n"
             "a word; Ctrl+L clears. Tab completes function names and file paths.\n"
             "On framebuffer displays, set zoom with:\n"
             "  (write (file /system/config/clearsh/zoom) (text \"2\"))\n"
             "Zoom values are 1-4; VGA text mode stays at native size.\n") - 1,
      0 },
    { "/BRAIN.TXT",
      "Brain window manager\n"
      "====================\n"
      "Brain manages terminal windows. Mod is either Alt key.\n"
      "  Mod+Enter       open a terminal\n"
      "  Mod+Q           close the focused window\n"
      "  Mod+Shift+Q     exit Brain\n"
      "  Mod+J / Mod+K   focus the left / right window\n"
      "  Mod+Shift+H/L   shrink / grow the master window\n"
      "  Mod+0..9        switch desktop\n"
      "  Mod+Shift+0..9  move focused window to desktop\n"
      "Window 1 shows logs on the configured log desktop.\n"
      "Edit /system/config/brain/layout for desktop count, startup/log\n"
      "desktops, master ratio/limits/step. Edit /system/config/brain/keys\n"
      "to change bindings. Edit /system/config/brain/theme for bar position\n"
      "and RGB colors (six hex digits without #). Changes apply on write.\n"
      "Edit /system/config/brain/statusbar for the script and interval_ms.\n"
      "The script writes display text to its configured output file.\n"
      "The old /system/config/brain.conf bar setting is a fallback.\n",
      sizeof("Brain window manager\n"
             "====================\n"
             "Brain manages terminal windows. Mod is either Alt key.\n"
             "  Mod+Enter       open a terminal\n"
             "  Mod+Q           close the focused window\n"
             "  Mod+Shift+Q     exit Brain\n"
             "  Mod+J / Mod+K   focus the left / right window\n"
             "  Mod+Shift+H/L   shrink / grow the master window\n"
             "  Mod+0..9        switch desktop\n"
             "  Mod+Shift+0..9  move focused window to desktop\n"
             "Window 1 shows logs on the configured log desktop.\n"
             "Edit /system/config/brain/layout for desktop count, startup/log\n"
             "desktops, master ratio/limits/step. Edit /system/config/brain/keys\n"
             "to change bindings. Edit /system/config/brain/theme for bar position\n"
             "and RGB colors (six hex digits without #). Changes apply on write.\n"
             "Edit /system/config/brain/statusbar for the script and interval_ms.\n"
             "The script writes display text to its configured output file.\n"
             "The old /system/config/brain.conf bar setting is a fallback.\n") - 1,
      0 },
    { "/SYSTEM.TXT",
      "System guide\n"
      "============\n"
      "Runtime information is under /system/info:\n"
      "  name, version, architecture, boot, features, uptime, memory,\n"
      "  storage, processes, syscalls\n"
      "Common kernel interfaces are files under /system/syscalls:\n"
      "  write, readkey, clear, shutdown, get_ticks, terminal_size, reap, sleep\n"
      "Runtime policy is configured with files under /system/config.\n"
      "See /BRAIN.TXT for desktop, key, and theme settings.\n"
      "Example: (print (read (file /system/syscalls/get_ticks)))\n"
      "The filesystem is RAM-backed. A Lucid disk image saves snapshots;\n"
      "without one, changes are lost on power off. Shutdown uses ACPI S5\n"
      "when available, with QEMU/Bochs legacy ports as fallback. QEMU exits\n"
      "when its guest powers off; the host remains running.\n"
      "The host-side (install) function only prints disk-image guidance.\n",
      sizeof("System guide\n"
             "============\n"
             "Runtime information is under /system/info:\n"
             "  name, version, architecture, boot, features, uptime, memory,\n"
             "  storage, processes, syscalls\n"
             "Common kernel interfaces are files under /system/syscalls:\n"
             "  write, readkey, clear, shutdown, get_ticks, terminal_size, reap, sleep\n"
             "Runtime policy is configured with files under /system/config.\n"
             "See /BRAIN.TXT for desktop, key, and theme settings.\n"
             "Example: (print (read (file /system/syscalls/get_ticks)))\n"
             "The filesystem is RAM-backed. A Lucid disk image saves snapshots;\n"
             "without one, changes are lost on power off. Shutdown uses ACPI S5\n"
             "when available, with QEMU/Bochs legacy ports as fallback. QEMU exits\n"
             "when its guest powers off; the host remains running.\n"
             "The host-side (install) function only prints disk-image guidance.\n") - 1,
      0 },
    { "/system/info/name", "Lucid\n", 6, 0 },
    { "/system/info/kernel", "Lucid Kernel\n", 13, 0 },
    { "/system/info/version", "0.1.0-pre\n", 10, 0 },
    { "/system/info/architecture", "x86\n", 4, 0 },
    { "/system/info/boot",
      "Kernel -> Pioneer -> Brain -> Eye -> Clearsh\n",
      sizeof("Kernel -> Pioneer -> Brain -> Eye -> Clearsh\n") - 1, 0 },
    { "/system/info/features",
      "ring3 user mode\nint 0x80 syscalls\nCOM1 and VGA console\n"
      "PS/2 keyboard\nRAMFS and ATA persistent LucidFS\n"
      "Brain tiled windows and configurable desktops (1-10)\n"
      "Eye terminal with Clearsh\nClearsh boot scripts\n"
      "Periodic Clearsh status-bar scripts\n",
      sizeof("ring3 user mode\nint 0x80 syscalls\nCOM1 and VGA console\n"
             "PS/2 keyboard\nRAMFS and ATA persistent LucidFS\n"
             "Brain tiled windows and configurable desktops (1-10)\n"
             "Eye terminal with Clearsh\nClearsh boot scripts\n"
             "Periodic Clearsh status-bar scripts\n") - 1, 0 },
    { "/system/info/syscalls",
      "int 0x80: EAX=id, EBX/ECX/EDX/ESI=args; EAX=result\n"
      "SYSCALL_POSIX_* failures return negative errno; older calls use 0xFFFFFFFF.\n"
      "0 write(ptr EBX,len ECX)\n1 exit(status EBX)\n"
      "2 readline(buffer EBX,capacity ECX,optional completion EDX/ESI)\n"
      "3 fs_read(path EBX,len ECX,out EDX,capacity ESI)\n"
      "4 fs_write(path EBX,len ECX,data EDX,len ESI)\n"
      "5 fs_list(path EBX,len ECX,out EDX,capacity ESI)\n"
      "6 readkey()  7 clear()  8 shutdown()  9 get_ticks()\n"
      "10 fork() 11 exec(path EBX,len ECX) 12 wait(pid EBX)\n"
      "13 reap() 14 terminal_size() 15 fs_stat(path EBX,len ECX)\n"
      "16 sleep(ticks EBX) 17 posix_open(path EBX,flags ECX)\n"
      "18 posix_read(fd EBX,buffer ECX,count EDX) 19 posix_write(fd EBX,buffer ECX,count EDX)\n"
      "20 posix_close(fd EBX) 21 posix_lseek(fd EBX,offset ECX,whence EDX)\n"
      "22 posix_unlink(path EBX) 23 posix_access(path EBX,mode ECX)\n"
      "24 posix_getcwd(buffer EBX,size ECX)\n"
      "POSIX.1-2008 file calls: 17-23; getcwd 24: POSIX.1-2001/2008\n"
      "Brain events: "
      "0x300 open, 0x301 exit, 0x302 reap\n"
      "File endpoints: /system/syscalls/{write,readkey,clear,shutdown,"
      "get_ticks,terminal_size,reap,sleep}\n"
      "Failure result: 0xFFFFFFFF. See SYSCALLS.md in the source tree.\n",
      sizeof("int 0x80: EAX=id, EBX/ECX/EDX/ESI=args; EAX=result\n"
             "SYSCALL_POSIX_* failures return negative errno; older calls use 0xFFFFFFFF.\n"
             "0 write(ptr EBX,len ECX)\n1 exit(status EBX)\n"
             "2 readline(buffer EBX,capacity ECX,optional completion EDX/ESI)\n"
             "3 fs_read(path EBX,len ECX,out EDX,capacity ESI)\n"
             "4 fs_write(path EBX,len ECX,data EDX,len ESI)\n"
             "5 fs_list(path EBX,len ECX,out EDX,capacity ESI)\n"
             "6 readkey()  7 clear()  8 shutdown()  9 get_ticks()\n"
             "10 fork() 11 exec(path EBX,len ECX) 12 wait(pid EBX)\n"
             "13 reap() 14 terminal_size() 15 fs_stat(path EBX,len ECX)\n"
             "16 sleep(ticks EBX) 17 posix_open(path EBX,flags ECX)\n"
             "18 posix_read(fd EBX,buffer ECX,count EDX) 19 posix_write(fd EBX,buffer ECX,count EDX)\n"
             "20 posix_close(fd EBX) 21 posix_lseek(fd EBX,offset ECX,whence EDX)\n"
             "22 posix_unlink(path EBX) 23 posix_access(path EBX,mode ECX)\n"
             "24 posix_getcwd(buffer EBX,size ECX)\n"
             "POSIX.1-2008 file calls: 17-23; getcwd 24: POSIX.1-2001/2008\n"
             "Brain events: "
             "0x300 open, 0x301 exit, 0x302 reap\n"
             "File endpoints: /system/syscalls/{write,readkey,clear,"
             "shutdown,get_ticks,terminal_size,reap,sleep}\n"
             "Failure result: 0xFFFFFFFF. See SYSCALLS.md in the source tree.\n") - 1,
      0 },
    { "/system/info/uptime", "", 0, 0 },
    { "/system/info/memory", "", 0, 0 },
    { "/system/info/storage", "", 0, 0 },
    { "/system/info/processes", "", 0, 0 },
    { "/system/config/pioneer/boot", "", 0, 0 },
    { "/system/config/pioneer/welcome.csh",
      "#!/system/bin/clearsh\n",
      sizeof("#!/system/bin/clearsh\n") - 1, 0 },
    { "/system/config/pioneer/system.csh",
      "#!/system/bin/clearsh\n"
      "(print (read (file /system/info/name)))\n",
      sizeof("#!/system/bin/clearsh\n"
             "(print (read (file /system/info/name)))\n") - 1, 0 },
    { "/users/root/example-scripts/hello.csh",
      "#!/system/bin/clearsh\n"
      "(print \"Hello from Clearsh.\")\n"
      "(print (concat \"System: \" (read (file /system/info/name))))\n",
      sizeof("#!/system/bin/clearsh\n"
             "(print \"Hello from Clearsh.\")\n"
             "(print (concat \"System: \" (read (file /system/info/name))))\n") - 1, 0 },
    { "/users/root/example-scripts/system-info.csh",
      "#!/system/bin/clearsh\n"
      "(print (concat \"Version: \" (read (file /system/info/version))))\n"
      "(print (concat \"Architecture: \" (read (file /system/info/architecture))))\n"
      "(print (concat \"Uptime (seconds): \" (text (uptime))))\n",
      sizeof("#!/system/bin/clearsh\n"
             "(print (concat \"Version: \" (read (file /system/info/version))))\n"
             "(print (concat \"Architecture: \" (read (file /system/info/architecture))))\n"
             "(print (concat \"Uptime (seconds): \" (text (uptime))))\n") - 1, 0 },
    { "/users/root/example-scripts/list-files.csh",
      "#!/system/bin/clearsh\n"
      "(print \"Files in /users/root:\")\n"
      "(ls (file /users/root))\n"
      "(print \"Example scripts:\")\n"
      "(ls (file /users/root/example-scripts))\n",
      sizeof("#!/system/bin/clearsh\n"
             "(print \"Files in /users/root:\")\n"
             "(ls (file /users/root))\n"
             "(print \"Example scripts:\")\n"
             "(ls (file /users/root/example-scripts))\n") - 1, 0 },
    { "/users/root/example-scripts/config.csh",
      "#!/system/bin/clearsh\n"
      "(print \"Configured applications:\")\n"
      "(print (concat \"Shell: \" (read (file /system/config/defaults/shell))))\n"
      "(print (concat \"Window manager: \" (read (file /system/config/defaults/window_manager))))\n"
      "(print (concat \"Terminal: \" (read (file /system/config/defaults/terminal))))\n",
      sizeof("#!/system/bin/clearsh\n"
             "(print \"Configured applications:\")\n"
             "(print (concat \"Shell: \" (read (file /system/config/defaults/shell))))\n"
             "(print (concat \"Window manager: \" (read (file /system/config/defaults/window_manager))))\n"
             "(print (concat \"Terminal: \" (read (file /system/config/defaults/terminal))))\n") - 1, 0 },
    { "/users/root/example-scripts/statusbar.csh",
      "#!/system/bin/clearsh\n"
      "(write (file /system/config/brain/statusbar_output) (concat \"uptime \" (text (uptime)) \"s\"))\n",
      sizeof("#!/system/bin/clearsh\n"
             "(write (file /system/config/brain/statusbar_output) (concat \"uptime \" (text (uptime)) \"s\"))\n") - 1, 0 },
    { "/users/root/example-scripts/write-note.csh",
      "#!/system/bin/clearsh\n"
      "(write (file /tmp/clearsh-note) (text \"Created by an example script.\\n\"))\n"
      "(print (read (file /tmp/clearsh-note)))\n",
      sizeof("#!/system/bin/clearsh\n"
             "(write (file /tmp/clearsh-note) (text \"Created by an example script.\\n\"))\n"
             "(print (read (file /tmp/clearsh-note)))\n") - 1, 0 },
    { "/users/root/welcome", "Welcome to Lucid.\n", 18, 0 }
};

const size_t ramfs_seed_count =
    sizeof(ramfs_seeds) / sizeof(ramfs_seeds[0]);
