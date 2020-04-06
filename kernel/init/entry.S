#include "mm/memlayout.h"
#include "mm/mmu.h"

# 获取虚拟地址对应的物理地址
#define REALLOC(x) (x - KERNBASE)

.text
.globl kern_entry
kern_entry:
    # 设置页目录表
    movl $REALLOC(__boot_pgdir), %eax
    mov %eax, %cr3

    # 使能页表映射
    movl %cr0, %eax
    orl $(CR0_PE | CR0_PG | CR0_AM | CR0_WP | CR0_NE | CR0_TS | CR0_EM | CR0_MP), %eax
    andl $~(CR0_TS | CR0_EM), %eax
    movl %eax, %cr0

    # 获取next的虚拟地址
    leal next, %eax

    # 跳转到KERNBASE开始的地址运行
    jmp *%eax

next:
    # 取消0～4M的虚拟地址映射
    xorl %eax, %eax
    movl %eax, __boot_pgdir

    movl $0x0, %ebp # 初始化EBP寄存器
    movl $bootstacktop, %esp # 设置ESP寄存器

    call kmain # 调用C函数

spin:
    jmp spin

.data
.align PGSIZE
    .globl bootstack
bootstack:
    .space KSTACKSIZE
    .globl bootstacktop
bootstacktop:

# 设置页目录表，将0～4M和KERNBASE～KERNBASE+4M虚拟地址空间映射到0～4M
.section .data.pgdir
.align PGSIZE
__boot_pgdir:
.globl __boot_pgdir
    .long REALLOC(__boot_pt1) + (PTE_P | PTE_U | PTE_W) # 设置0～4M对应的页表
    .space (KERNBASE >> PGSHIFT >> 10 << 2) - (. - __boot_pgdir)
    .long REALLOC(__boot_pt1) + (PTE_P | PTE_U | PTE_W) # 设置KERNBASE～KERNBASE+4M对应的页表
    .space PGSIZE - (. - __boot_pgdir)

# 页表，映射0～4M物理地址空间
.set i, 0
__boot_pt1:
.rept 1024
    .long i * PGSIZE + (PTE_P | PTE_W)
    .set i, i + 1
.endr
