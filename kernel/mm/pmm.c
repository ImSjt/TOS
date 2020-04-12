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
#include "error.h"
#include "assert.h"

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

struct taskstate ts = {0};

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

pte_t * const vpt = (pte_t *)VPT;
pde_t * const vpd = (pde_t *)PGADDR(PDX(VPT), PDX(VPT), 0);

static inline void page_remove_pte(pde_t *pgdir, uintptr_t la, pte_t *ptep);

static void gdt_init() {
    load_esp0((uintptr_t)bootstacktop);
    ts.ts_ss0 = KERNEL_DS;

    // 初始化TSS段描述符
    gdt[SEG_TSS] = SEGTSS(STS_T32A, (uintptr_t)&ts, sizeof(ts), DPL_KERNEL);

    // 重新加载GDT
    lgdt(&gdt_pd);

    // 加载TSS
    ltr(GD_TSS);
}

static void init_memmap(struct page *base, size_t n) {
    pmm_manager->init_memmap(base, n);
}

static void
page_init(void) {
    struct e820map *memmap = (struct e820map *)(0x8000 + KERNBASE);
    uint64_t maxpa = 0;

    printk("e820map:\n");
    int i;
    for (i = 0; i < memmap->nr_map; i ++) {
        uint64_t begin = memmap->map[i].addr, end = begin + memmap->map[i].size;
        printk("  memory: %08llx, [%08llx, %08llx], type = %d.\n",
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
    printk("memory management: %s\n", pmm_manager->name);
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

void load_esp0(uintptr_t esp0) {
    ts.ts_esp0 = esp0;
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
        // printk("---%p---%d\n", pa, page2ppn(page));
        memset(KADDR(pa), 0, PGSIZE);
        *pdep = pa | PTE_U | PTE_W | PTE_P;
    }

    // PDE_ADDR(*pdep)得到页目录项的内容（即页表地址），然后将其转化为虚拟地址，再取得对应的页表项的地址
    return &((pte_t *)KADDR(PDE_ADDR(*pdep)))[PTX(la)];
}

int copy_range(pde_t *to, pde_t *from, uintptr_t start, uintptr_t end, bool share) {
    assert(start % PGSIZE == 0 && end % PGSIZE == 0);
    assert(USER_ACCESS(start, end));
    // copy content by page unit.
    do {
        //call get_pte to find process A's pte according to the addr start
        pte_t *ptep = get_pte(from, start, 0), *nptep;
        if (ptep == NULL) {
            start = ROUNDDOWN(start + PTSIZE, PTSIZE);
            continue ;
        }
        //call get_pte to find process B's pte according to the addr start. If pte is NULL, just alloc a PT
        if (*ptep & PTE_P) {
            if ((nptep = get_pte(to, start, 1)) == NULL) {
                return -E_NO_MEM;
            }
        uint32_t perm = (*ptep & PTE_USER);
        //get page from ptep
        struct page *page = pte2page(*ptep);
        // alloc a page for process B
        struct page *npage=alloc_page();
        assert(page!=NULL);
        assert(npage!=NULL);
        int ret=0;
        /* LAB5:EXERCISE2 YOUR CODE
         * replicate content of page to npage, build the map of phy addr of nage with the linear addr start
         *
         * Some Useful MACROs and DEFINEs, you can use them in below implementation.
         * MACROs or Functions:
         *    page2kva(struct page *page): return the kernel vritual addr of memory which page managed (SEE pmm.h)
         *    page_insert: build the map of phy addr of an page with the linear addr la
         *    memcpy: typical memory copy function
         *
         * (1) find src_kvaddr: the kernel virtual address of page
         * (2) find dst_kvaddr: the kernel virtual address of npage
         * (3) memory copy from src_kvaddr to dst_kvaddr, size is PGSIZE
         * (4) build the map of phy addr of  nage with the linear addr start
         */
        void * kva_src = page2kva(page);
        void * kva_dst = page2kva(npage);
    
        memcpy(kva_dst, kva_src, PGSIZE);

        ret = page_insert(to, npage, start, perm);
        assert(ret == 0);
        }
        start += PGSIZE;
    } while (start != 0 && start < end);
    return 0;
}

void page_remove(pde_t *pgdir, uintptr_t la) {
    pte_t *ptep = get_pte(pgdir, la, 0);
    if (ptep != NULL) {
        page_remove_pte(pgdir, la, ptep);
    }
}

struct page *pgdir_alloc_page(pde_t *pgdir, uintptr_t la, uint32_t perm) {
    struct page *page = alloc_page();
    if (page != NULL) {
        if (page_insert(pgdir, page, la, perm) != 0) {
            free_page(page);
            return NULL;
        }
    }

    return page;
}

int page_insert(pde_t *pgdir, struct page *page, uintptr_t la, uint32_t perm) {
    pte_t *ptep = get_pte(pgdir, la, 1);
    if (ptep == NULL) {
        return -E_NO_MEM;
    }
    page_ref_inc(page);
    if (*ptep & PTE_P) {
        struct page *p = pte2page(*ptep);
        if (p == page) {
            page_ref_dec(page);
        }
        else {
            page_remove_pte(pgdir, la, ptep);
        }
    }
    *ptep = page2pa(page) | PTE_P | perm;
    tlb_invalidate(pgdir, la);
    return 0;
}

void tlb_invalidate(pde_t *pgdir, uintptr_t la) {
    if (rcr3() == PADDR(pgdir)) {
        invlpg((void *)la);
    }
}

static inline void page_remove_pte(pde_t *pgdir, uintptr_t la, pte_t *ptep) {
    if (*ptep & PTE_P) {
        struct page *page = pte2page(*ptep);
        if (page_ref_dec(page) == 0) {
            free_page(page);
        }
        *ptep = 0;
        tlb_invalidate(pgdir, la);
    }
}


void unmap_range(pde_t *pgdir, uintptr_t start, uintptr_t end) {
    assert(start % PGSIZE == 0 && end % PGSIZE == 0);
    assert(USER_ACCESS(start, end));

    do {
        pte_t *ptep = get_pte(pgdir, start, 0);
        if (ptep == NULL) {
            start = ROUNDDOWN(start + PTSIZE, PTSIZE);
            continue ;
        }
        if (*ptep != 0) {
            page_remove_pte(pgdir, start, ptep);
        }
        start += PGSIZE;
    } while (start != 0 && start < end);
}

void exit_range(pde_t *pgdir, uintptr_t start, uintptr_t end) {
    assert(start % PGSIZE == 0 && end % PGSIZE == 0);
    assert(USER_ACCESS(start, end));

    start = ROUNDDOWN(start, PTSIZE);
    do {
        int pde_idx = PDX(start);
        if (pgdir[pde_idx] & PTE_P) {
            free_page(pde2page(pgdir[pde_idx]));
            pgdir[pde_idx] = 0;
        }
        start += PTSIZE;
    } while (start != 0 && start < end);
}

static int get_pgtable_items(size_t left, size_t right, size_t start, uintptr_t *table, size_t *left_store, size_t *right_store) {
    if (start >= right) {
        return 0;
    }
    while (start < right && !(table[start] & PTE_P)) {
        start ++;
    }
    if (start < right) {
        if (left_store != NULL) {
            *left_store = start;
        }
        int perm = (table[start ++] & PTE_USER);
        while (start < right && (table[start] & PTE_USER) == perm) {
            start ++;
        }
        if (right_store != NULL) {
            *right_store = start;
        }
        return perm;
    }
    return 0;
}

static const char *perm2str(int perm) {
    static char str[4];
    str[0] = (perm & PTE_U) ? 'u' : '-';
    str[1] = 'r';
    str[2] = (perm & PTE_W) ? 'w' : '-';
    str[3] = '\0';
    return str;
}

void print_pgdir(void) {
    printk("-------------------- BEGIN --------------------\n");
    size_t left, right = 0, perm;
    while ((perm = get_pgtable_items(0, NPDEENTRY, right, vpd, &left, &right)) != 0) {
        printk("PDE(%03x) %08x-%08x %08x %s\n", right - left,
                left * PTSIZE, right * PTSIZE, (right - left) * PTSIZE, perm2str(perm));
        size_t l, r = left * NPTEENTRY;
        while ((perm = get_pgtable_items(left * NPTEENTRY, right * NPTEENTRY, r, vpt, &l, &r)) != 0) {
            printk("  |-- PTE(%05x) %08x-%08x %08x %s\n", r - l,
                    l * PGSIZE, r * PGSIZE, (r - l) * PGSIZE, perm2str(perm));
        }
    }
    printk("--------------------- END ---------------------\n");
}