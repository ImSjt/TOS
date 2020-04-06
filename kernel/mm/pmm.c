#include "stdio.h"
#include "defs.h"
#include "x86.h"
#include "string.h"
#include "mm/pmm.h"
#include "mm/mmu.h"
#include "mm/memlayout.h"
#include "mm/default_pmm.h"
#include "mm/buddy_pmm.h"
#include "sync/sync.h"
#include "list.h"
#include "mm/kmalloc.h"

// 页目录表
extern pde_t __boot_pgdir;
pde_t *boot_pgdir = &__boot_pgdir;
uintptr_t boot_cr3;

// 物理内存管理器
const struct pmm_manager *pmm_manager;

// 物理页数组
struct page *pages;

// 物理页数量
size_t npage = 0;

static struct segdesc gdt[] = {
    SEG_NULL,
    [SEG_KTEXT] = SEG(STA_X | STA_R, 0x0, 0xFFFFFFFF, DPL_KERNEL),
    [SEG_KDATA] = SEG(STA_W, 0x0, 0xFFFFFFFF, DPL_KERNEL),
    [SEG_UTEXT] = SEG(STA_X | STA_R, 0x0, 0xFFFFFFFF, DPL_USER),
    [SEG_UDATA] = SEG(STA_W, 0x0, 0xFFFFFFFF, DPL_USER),
    [SEG_TSS]   = SEG_NULL,
};

static struct pseudodesc gdt_pd = {
    sizeof(gdt) - 1, (uintptr_t)gdt
};

static inline void lgdt(struct pseudodesc *pd) {
    asm volatile ("lgdt (%0)" :: "r" (pd));
    asm volatile ("movw %%ax, %%gs" :: "a" (USER_DS));
    asm volatile ("movw %%ax, %%fs" :: "a" (USER_DS));
    asm volatile ("movw %%ax, %%es" :: "a" (KERNEL_DS));
    asm volatile ("movw %%ax, %%ds" :: "a" (KERNEL_DS));
    asm volatile ("movw %%ax, %%ss" :: "a" (KERNEL_DS));
    // reload cs
    asm volatile ("ljmp %0, $1f\n 1:\n" :: "i" (KERNEL_CS));
}

static void gdt_init() {
    lgdt(&gdt_pd);
}

static void init_memmap(struct page *base, size_t n) {
    pmm_manager->init_memmap(base, n);
}

static void
page_init(void) {
    struct e820map *memmap = (struct e820map *)(0x8000 + KERNBASE);
    uint64_t maxpa = 0;

    kprint("e820map:\n");
    int i;
    for (i = 0; i < memmap->nr_map; i ++) {
        uint64_t begin = memmap->map[i].addr, end = begin + memmap->map[i].size;
        kprint("  memory: %08llx, [%08llx, %08llx], type = %d.\n",
                memmap->map[i].size, begin, end - 1, memmap->map[i].type);
        if (memmap->map[i].type == E820_ARM) {
            if (maxpa < end && begin < KMEMSIZE) {
                maxpa = end;
            }
        }
    }
    if (maxpa > KMEMSIZE) {
        maxpa = KMEMSIZE;
    }

    extern char end[];

    npage = maxpa / PGSIZE;
    pages = (struct page *)ROUNDUP((void *)end, PGSIZE);

    for (i = 0; i < npage; i ++) {
        SetpageReserved(pages + i);
    }

    uintptr_t freemem = PADDR((uintptr_t)pages + sizeof(struct page) * npage); // pages所在的物理内存是不可以被使用的
       for (i = 0; i < memmap->nr_map; i ++) {
        uint64_t begin = memmap->map[i].addr, end = begin + memmap->map[i].size;
        if (memmap->map[i].type == E820_ARM) {
            if (begin < freemem) {
                begin = freemem;
            }
            if (end > KMEMSIZE) {
                end = KMEMSIZE;
            }
            if (begin < end) {
                begin = ROUNDUP(begin, PGSIZE);
                end = ROUNDDOWN(end, PGSIZE);
                if (begin < end) {
                    init_memmap(pa2page(begin), (end - begin) / PGSIZE);
                }
            }
        }
    }
}

static void init_pmm_manager(void) {
    pmm_manager = &default_pmm_manager; // 默认的物理内存管理器
    kprint("memory management: %s\n", pmm_manager->name);
    pmm_manager->init();
}

static void boot_map_segment(pde_t *pgdir, uintptr_t la, size_t size, uintptr_t pa, uint32_t perm) {
    size_t n = ROUNDUP(size + PGOFF(la), PGSIZE) / PGSIZE;
    la = ROUNDDOWN(la, PGSIZE);
    pa = ROUNDDOWN(pa, PGSIZE);
    for (; n > 0; n --, la += PGSIZE, pa += PGSIZE) {
        pte_t *ptep = get_pte(pgdir, la, 1); // get_pte获取虚拟地址对应页表项
        *ptep = pa | PTE_P | perm;
    }
}

void pmm_init(void) {
    boot_cr3 = PADDR(boot_pgdir);

    // 初始化物理内存管理器
    init_pmm_manager();

    // 初始化物理页
    page_init();
    pmm_manager->dump_area();

    // 自映射，可以通过访问VPT开始的4M空间访问访问页表内容
    boot_pgdir[PDX(VPT)] = PADDR(boot_pgdir) | PTE_P | PTE_W;

    // 建立映射，将0开始处物理地址映射到KERNBASE开始处的虚拟地址，大小为KMEMSIZE
    boot_map_segment(boot_pgdir, KERNBASE, KMEMSIZE, 0, PTE_W);

    // 重新加载GDT
    gdt_init();

    kmalloc_init();
}

struct page *alloc_pages(int order) {
    struct page *page=NULL;
    bool intr_flag;
    
    while (1)
    {
         local_intr_save(intr_flag); // 禁止中断
         {
              page = pmm_manager->alloc_pages(order);
         }
         local_intr_restore(intr_flag); // 恢复中断

         if (page != NULL || order > 1) break;
    }
    return page;
}

void free_pages(struct page *base, int order) {
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        pmm_manager->free_pages(base, order);
    }
    local_intr_restore(intr_flag);
}

size_t
nr_free_pages(void) {
    size_t ret;
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        ret = pmm_manager->nr_free_pages();
    }
    local_intr_restore(intr_flag);
    return ret;
}

// 获取虚拟地址对应的物理地址
pte_t *get_pte(pde_t *pgdir, uintptr_t la, bool create) {
    pde_t *pdep = &pgdir[PDX(la)]; // 获取页目录项
    if (!(*pdep & PTE_P)) { // 如果页目录项没有对应页表，那么就为其分配
        struct page *page;
        if (!create || (page = alloc_page()) == NULL) {
            return NULL;
        }
        set_page_ref(page, 1);
        uintptr_t pa = page2pa(page);
        // kprint("---%p---%d\n", pa, page2ppn(page));
        memset(KADDR(pa), 0, PGSIZE);
        *pdep = pa | PTE_U | PTE_W | PTE_P;
    }

    // PDE_ADDR(*pdep)得到页目录项的内容（即页表地址），然后将其转化为虚拟地址，再取得对应的页表项的地址
    return &((pte_t *)KADDR(PDE_ADDR(*pdep)))[PTX(la)];
}