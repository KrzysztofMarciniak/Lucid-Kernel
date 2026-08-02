INIT_BOOT=terminal,gdt,idt,pic,timer,pmm,vmm,heap,usermode

TERMINAL_IMPL=com1
GDT_IMPL=flat
KERNEL_PANIC_IMPL=simple
IDT_IMPL=array256
PIC_IMPL=simple8259
TIMER_IMPL=pit100hz
PMM_IMPL=bitmap4kb
VMM_IMPL=twolevel
HEAP_IMPL=bumpheap
KMALLOC_TEST_IMPL=basic
USERMODE_IMPL=iret3

MEMORY_MB=256
