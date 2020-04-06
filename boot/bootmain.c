#include "defs.h"
#include "x86.h"
#include "elf.h"

#define SECTSIZE 512 // 扇区大小
#define ELFHDR ((struct elfhdr *)0x10000) // 将内核elf文件的头部放置的位置

// 等待磁盘准备好
static void waitDisk(void)
{
    while ((inb(0x1F7) & 0xC0) != 0x40);
}

// 读扇区
static void readSect(void* dst, uint32_t secno)
{
    waitDisk();

    outb(0x1F2, 1); // count = 1
    outb(0x1F3, secno & 0xFF);
    outb(0x1F4, (secno >> 8) & 0xFF);
    outb(0x1F5, (secno >> 16) & 0xFF);
    outb(0x1F6, ((secno >> 24) & 0xF) | 0xE0);
    outb(0x1F7, 0x20); // cmd 0x20 - read sectors

    waitDisk();

    insl(0x1F0, dst, SECTSIZE / 4);
}

// 读取多个扇区
static void readSeg(uintptr_t va, uint32_t count, uint32_t offset)
{
    uintptr_t endVa = va + count; // 虚拟地址结束的位置

    va -= va % SECTSIZE; // 向下扇区对齐

    uint32_t secno = (offset / SECTSIZE) + 1; // 获取内核文件在磁盘中的起始扇区，由于第一个扇区用来作为启动盘，所以需要加1

    // 一个扇区一个扇区地读
    for (; va < endVa; va += SECTSIZE, secno++)
    {
        readSect((void*)va, secno);
    }
}

void bootmain(void)
{
    // 读取elf头部
    readSeg((uintptr_t)ELFHDR, SECTSIZE * 8, 0);

    if (ELFHDR->e_magic != ELF_MAGIC)
    {
        goto bad;
    }

    // 将elf文件中所有的段读到内存
    struct proghdr *ph, *eph;
    ph = (struct proghdr *)((uintptr_t)ELFHDR + ELFHDR->e_phoff);
    eph = ph + ELFHDR->e_phnum;
    for (; ph < eph; ph ++) {
        readSeg(ph->p_va & 0xFFFFFF, ph->p_memsz, ph->p_offset); // 为什么&0xFFFFFF？只取低24位，获得虚拟地址对应的物理地址
    }

    // 跳转到内核入口执行，
    ((void (*)(void))(ELFHDR->e_entry & 0xFFFFFF))();

bad:
    outw(0x8A00, 0x8A00);
    outw(0x8A00, 0x8E00);

    while (1);
}