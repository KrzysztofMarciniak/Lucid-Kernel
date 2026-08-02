INIT_BOOT=terminal,gdt,idt,pic,timer,pmm,vmm,heap

TERMINAL_IMPL=com1
GDT_IMPL=flat
KERNEL_PANIC_IMPL=simple
IDT_IMPL=array256
PIC_IMPL=simple8259
TIMER_IMPL=pit100hz
PMM_IMPL=bitmap4kb
VMM_IMPL=twolevel
HEAP_IMPL=bumpheap

MEMORY_MB=256
