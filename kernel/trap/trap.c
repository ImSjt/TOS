#include "stdio.h"
#include "defs.h"
#include "x86.h"
#include "error.h"
#include "unistd.h"
#include "trap/trap.h"
#include "mm/memlayout.h"
#include "mm/mmu.h"
#include "driver/intr.h"
#include "sync/sync.h"
#include "driver/console.h"
#include "mm/vmm.h"
#include "assert.h"
#include "schedule/sched.h"
#include "process/task.h"
#include "syscall/syscall.h"

// 中断描述符
static struct gatedesc idt[256] = {{0}};

static struct pseudodesc idt_pd = {
    sizeof(idt) - 1, (uintptr_t)idt
};

void idt_init(void) {
    extern uintptr_t __vectors[];

    // 初始化中断描述符表
    int i;
    for (i = 0; i < sizeof(idt) / sizeof(struct gatedesc); i++) {
        SETGATE(idt[i], 0, GD_KTEXT, __vectors[i], DPL_KERNEL);
    }

    SETGATE(idt[T_SYSCALL], 1, GD_KTEXT, __vectors[T_SYSCALL], DPL_USER);
    
    lidt(&idt_pd);
}

static const char *IA32flags[] = {
    "CF", NULL, "PF", NULL, "AF", NULL, "ZF", "SF",
    "TF", "IF", "DF", "OF", NULL, NULL, "NT", NULL,
    "RF", "VM", "AC", "VIF", "VIP", "ID", NULL, NULL,
};

static const char* trapname(int trapno) {
    static const char * const excnames[] = {
        "Divide error",
        "Debug",
        "Non-Maskable Interrupt",
        "Breakpoint",
        "Overflow",
        "BOUND Range Exceeded",
        "Invalid Opcode",
        "Device Not Available",
        "Double Fault",
        "Coprocessor Segment Overrun",
        "Invalid TSS",
        "Segment Not Present",
        "Stack Fault",
        "General Protection",
        "page Fault",
        "(unknown trap)",
        "x87 FPU Floating-Point Error",
        "Alignment Check",
        "Machine-Check",
        "SIMD Floating-Point Exception"
    };

    if (trapno < sizeof(excnames)/sizeof(const char * const)) {
        return excnames[trapno];
    }
    if (trapno >= IRQ_OFFSET && trapno < IRQ_OFFSET + 16) {
        return "Hardware Interrupt";
    }
    return "(unknown trap)";
}

void print_trapframe(struct trapframe *tf) {
    printk("trapframe at %p\n", tf);
    print_regs(&tf->tf_regs);
    printk("  ds   0x----%04x\n", tf->tf_ds);
    printk("  es   0x----%04x\n", tf->tf_es);
    printk("  fs   0x----%04x\n", tf->tf_fs);
    printk("  gs   0x----%04x\n", tf->tf_gs);
    printk("  trap 0x%08x %s\n", tf->tf_trapno, trapname(tf->tf_trapno));
    printk("  err  0x%08x\n", tf->tf_err);
    printk("  eip  0x%08x\n", tf->tf_eip);
    printk("  cs   0x----%04x\n", tf->tf_cs);
    printk("  flag 0x%08x ", tf->tf_eflags);

    int i, j;
    for (i = 0, j = 1; i < sizeof(IA32flags) / sizeof(IA32flags[0]); i ++, j <<= 1) {
        if ((tf->tf_eflags & j) && IA32flags[i] != NULL) {
            printk("%s,", IA32flags[i]);
        }
    }
    printk("IOPL=%d\n", (tf->tf_eflags & FL_IOPL_MASK) >> 12);

    if (!trap_in_kernel(tf)) {
        printk("  esp  0x%08x\n", tf->tf_esp);
        printk("  ss   0x----%04x\n", tf->tf_ss);
    }
}

void print_regs(struct pushregs *regs) {
    printk("  edi  0x%08x\n", regs->reg_edi);
    printk("  esi  0x%08x\n", regs->reg_esi);
    printk("  ebp  0x%08x\n", regs->reg_ebp);
    printk("  oesp 0x%08x\n", regs->reg_oesp);
    printk("  ebx  0x%08x\n", regs->reg_ebx);
    printk("  edx  0x%08x\n", regs->reg_edx);
    printk("  ecx  0x%08x\n", regs->reg_ecx);
    printk("  eax  0x%08x\n", regs->reg_eax);
}

bool trap_in_kernel(struct trapframe *tf) {
    return (tf->tf_cs == (uint16_t)KERNEL_CS);
}

static int pgfault_handler(struct trapframe *tf) {
    extern struct mm_struct *check_mm_struct;
    struct mm_struct *mm = check_mm_struct;

    return do_pgfault(mm, tf->tf_err, rcr2());
}

static void trap_dispatch(struct trapframe *tf) {
    int ret;

    switch (tf->tf_trapno)
    {
    // 缺页中断
    case T_PGFLT:
        printk("page fault!");
        if ((ret = pgfault_handler(tf)) != 0) {
            print_trapframe(tf);
            panic("handle pgfault failed. ret=%d\n", ret);
        }
        break;

    case T_SYSCALL:
        syscall();
        break;

    // 定时器中断
    case IRQ_OFFSET + IRQ_TIMER:
        run_timer_list();
        break;

    // 键盘中断
    case IRQ_OFFSET + IRQ_KBD:
        printk("%d ", cons_getc());
        break;

    default:
        break;
    }
}

void trap(struct trapframe *tf) {
    if (current == NULL) {
        trap_dispatch(tf);
    } else {
        struct trapframe *otf = current->tf;
        current->tf = tf;

        bool in_kernel = trap_in_kernel(tf);
    
        trap_dispatch(tf);
    
        current->tf = otf;
        if (!in_kernel) {
            if (current->flags & PF_EXITING) {
                do_exit(-E_KILLED);
            }
            if (current->need_resched) {
                schedule();
            }
        }
    }
}