#include "stdio.h"
#include "defs.h"
#include "x86.h"
#include "trap/trap.h"
#include "mm/memlayout.h"
#include "mm/mmu.h"
#include "driver/intr.h"
#include "sync/sync.h"
#include "driver/console.h"

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

    // SETGATE(idt[T_SYSCALL], 1, GD_KTEXT, __vectors[T_SYSCALL], DPL_USER);
    
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
    kprint("trapframe at %p\n", tf);
    print_regs(&tf->tf_regs);
    kprint("  ds   0x----%04x\n", tf->tf_ds);
    kprint("  es   0x----%04x\n", tf->tf_es);
    kprint("  fs   0x----%04x\n", tf->tf_fs);
    kprint("  gs   0x----%04x\n", tf->tf_gs);
    kprint("  trap 0x%08x %s\n", tf->tf_trapno, trapname(tf->tf_trapno));
    kprint("  err  0x%08x\n", tf->tf_err);
    kprint("  eip  0x%08x\n", tf->tf_eip);
    kprint("  cs   0x----%04x\n", tf->tf_cs);
    kprint("  flag 0x%08x ", tf->tf_eflags);

    int i, j;
    for (i = 0, j = 1; i < sizeof(IA32flags) / sizeof(IA32flags[0]); i ++, j <<= 1) {
        if ((tf->tf_eflags & j) && IA32flags[i] != NULL) {
            kprint("%s,", IA32flags[i]);
        }
    }
    kprint("IOPL=%d\n", (tf->tf_eflags & FL_IOPL_MASK) >> 12);

    if (!trap_in_kernel(tf)) {
        kprint("  esp  0x%08x\n", tf->tf_esp);
        kprint("  ss   0x----%04x\n", tf->tf_ss);
    }
}

void print_regs(struct pushregs *regs) {
    kprint("  edi  0x%08x\n", regs->reg_edi);
    kprint("  esi  0x%08x\n", regs->reg_esi);
    kprint("  ebp  0x%08x\n", regs->reg_ebp);
    kprint("  oesp 0x%08x\n", regs->reg_oesp);
    kprint("  ebx  0x%08x\n", regs->reg_ebx);
    kprint("  edx  0x%08x\n", regs->reg_edx);
    kprint("  ecx  0x%08x\n", regs->reg_ecx);
    kprint("  eax  0x%08x\n", regs->reg_eax);
}

bool trap_in_kernel(struct trapframe *tf) {
    return (tf->tf_cs == (uint16_t)KERNEL_CS);
}

static void trap_dispatch(struct trapframe *tf) {
    int c;
    switch (tf->tf_trapno)
    {
    case IRQ_OFFSET + IRQ_TIMER:
        break;

    case IRQ_OFFSET + IRQ_KBD:
        c = cons_getc();
        kprint("%d ", c);
        break;

    default:
        break;
    }
}

void trap(struct trapframe *tf) {
    trap_dispatch(tf);
}