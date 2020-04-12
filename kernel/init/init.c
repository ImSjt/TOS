#include "stdio.h"
#include "string.h"
#include "assert.h"
#include "driver/console.h"
#include "driver/picirq.h"
#include "driver/clock.h"
#include "driver/intr.h"
#include "trap/trap.h"
#include "mm/pmm.h"
#include "mm/kmalloc.h"
#include "mm/vmm.h"
#include "process/task.h"
#include "schedule/sched.h"

void cpu_idle(void);

void kmain(void)
{
    extern char edata[], end[];
    memset(edata, 0, end - edata);
    
    cons_init(); // 初始化控制台
    printk("Hello World!");

    pmm_init(); // 物理内存初始化

    pic_init(); // 初始化中断控制器

    idt_init(); // 中断门初始化

    vmm_init(); // 虚拟内存管理初始化

    sched_init(); // 调度初始化
    task_init(); // 初始化进程，idel进程为当前进程

    clock_init(); // 时钟初始化

    intr_enable(); // 使能中断

    cpu_idle();
}

void cpu_idle(void) {
    while (1) {
        if (current->need_resched) {
            schedule();
        }
    }
}