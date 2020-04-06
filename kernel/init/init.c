#include "stdio.h"
#include "string.h"
#include "driver/console.h"
#include "driver/picirq.h"
#include "driver/clock.h"
#include "driver/intr.h"
#include "trap/trap.h"
#include "mm/pmm.h"
#include "mm/kmalloc.h"

void kmain(void)
{
    extern char edata[], end[];
    memset(edata, 0, end - edata);
    
    cons_init(); // 初始化控制台
    kprint("Hello World!");

    pmm_init(); // 物理内存初始化

    pic_init(); // 初始化中断控制器

    idt_init(); // 中断门初始化

    clock_init(); // 时钟初始化

    intr_enable(); // 使能中断

    while(1);
}