INIT_BOOT=terminal,gdt,idt,pic,timer,pmm,acpi,vmm,heap,ata,usermode,ramfs,syscall,init
SYSCALLS=fs_read,fs_write

TERMINAL_IMPL=dual
GDT_IMPL=flat
KERNEL_PANIC_IMPL=simple
IDT_IMPL=array256
PIC_IMPL=simple8259
TIMER_IMPL=pit250hz
ACPI_IMPL=basic
PMM_IMPL=bitmap4kb
VMM_IMPL=twolevel
HEAP_IMPL=bumpheap
ATA_IMPL=pio
RAMFS_IMPL=static
INIT_IMPL=bootstrap
KMALLOC_TEST_IMPL=basic
USERMODE_IMPL=iret3
SYSCALL_IMPL=int80
SYSCALL_FS_READ_IMPL=basic
SYSCALL_FS_WRITE_IMPL=basic

MEMORY_MB=256
LOW_RESERVED_MB=4
