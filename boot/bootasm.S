#include "asm.h"

.set PROT_MODE_CSEG,        0x8                     # 内核代码段选择子
.set PROT_MODE_DSEG,        0x10                    # 内核数据段选择子
.set CR0_PE_ON,             0x1                     # 保护模式使能位
.set SMAP,                  0x534d4150              

.globl start
start:
.code16 # 在没开启保护模式之前是16位代码
    cli # 关中断
    cld # 设置edi寄存器值的增长方向

    # 设置段寄存器
    xorw %ax, %ax # 清零
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss 

    # 开始A20地址线
seta20.1:
    inb $0x64, %al                                  # 等待不繁忙
    testb $0x2, %al
    jnz seta20.1

    movb $0xd1, %al                                 # 0xd1 -> port 0x64
    outb %al, $0x64

seta20.2:
    inb $0x64, %al                                  # 等待不繁忙
    testb $0x2, %al
    jnz seta20.2

    movb $0xdf, %al                                 # 0xdf -> port 0x60
    outb %al, $0x60

    # 探测内存，将数据放到0x8000处
probe_memory:
    movl $0, 0x8000
    xorl %ebx, %ebx
    movw $0x8004, %di
start_probe:
    movl $0xE820, %eax
    movl $20, %ecx
    movl $SMAP, %edx
    int $0x15
    jnc cont
    movw $12345, 0x8000
    jmp finish_probe
cont:
    addw $20, %di
    incl 0x8000
    cmpl $0, %ebx
    jnz start_probe
finish_probe:

    # 加载GDT
    lgdt gdtdesc
    # 开启保护模式
    movl %cr0, %eax
    orl $CR0_PE_ON, %eax
    movl %eax, %cr0

    # 通过长跳转真正进入保护模式运行
    ljmp $PROT_MODE_CSEG, $protcseg

.code32
protcseg:
    # 重新加载各个段寄存器
    movw $PROT_MODE_DSEG, %ax                       # 内核数据段选择子
    movw %ax, %ds                                   # -> DS
    movw %ax, %es                                   # -> ES
    movw %ax, %fs                                   # -> FS
    movw %ax, %gs                                   # -> GS
    movw %ax, %ss                                   # -> SS

    movl $0x0, %ebp # 初始化EBP寄存器
    movl $start, %esp # 设置好栈顶指针0x7C00
    call bootmain # 跳转到C函数运行

# 如果bootmain返回则自旋
spin:
    jmp spin

.data
.p2align 2 # 4字节对齐
gdt:
    SEG_NULLASM # 第一个段描述符为空
    SEG_ASM(STA_X|STA_R, 0x0, 0xffffffff) # 内核代码段
    SEG_ASM(STA_W, 0x0, 0xffffffff) # 内核数据段

gdtdesc:
    .word 0x17
    .long gdt