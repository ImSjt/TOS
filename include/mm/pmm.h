#ifndef __MM_PMM_H__
#define __MM_PMM_H__
#include "mm/memlayout.h"
#include "mm/mmu.h"

extern const struct pmm_manager *pmm_manager;
extern pde_t *boot_pgdir;
extern uintptr_t boot_cr3;

// 虚拟地址转物理地址
#define PADDR(kva) ({                                                   \
            uintptr_t __m_kva = (uintptr_t)(kva);                       \
            __m_kva - KERNBASE;                                         \
        })

// 物理地址转换为虚拟地址
#define KADDR(pa) ({                                                    \
            uintptr_t __m_pa = (pa);                                    \
            (void *) (__m_pa + KERNBASE);                               \
        })

// 物理内存管理器
struct pmm_manager {
    const char *name;                                 // XXX_pmm_manager's name
    void (*init)(void);                               // initialize internal description&management data structure
                                                      // (free block list, number of free block) of XXX_pmm_manager 
    void (*init_memmap)(struct page *base, size_t n); // setup description&management data structcure according to
                                                      // the initial free physical memory space 
    struct page *(*alloc_pages)(int order);            // allocate >=n pages, depend on the allocation algorithm 
    void (*free_pages)(struct page *base, int order);  // free >=n pages with "base" addr of page descriptor structures(memlayout.h)
    size_t (*nr_free_pages)(void);                    // return the number of free pages
    void (*dump_area)(void);
};

extern struct page *pages;
extern size_t npage;

static inline ppn_t
page2ppn(struct page *page) {
    return page - pages;
}

static inline uintptr_t
page2pa(struct page *page) {
    return page2ppn(page) << PGSHIFT;
}

static inline struct page *
pa2page(uintptr_t pa) {
    if (PPN(pa) >= npage) {
        return NULL;
    }
    return &pages[PPN(pa)];
}

static inline void *
page2kva(struct page *page) {
    return KADDR(page2pa(page));
}

static inline struct page *
kva2page(void *kva) {
    return pa2page(PADDR(kva));
}

static inline struct page *
pte2page(pte_t pte) {
    if (!(pte & PTE_P)) {
        return NULL;
    }
    return pa2page(PTE_ADDR(pte));
}

static inline struct page *
pde2page(pde_t pde) {
    return pa2page(PDE_ADDR(pde));
}

static inline int
page_ref(struct page *page) {
    return page->ref;
}

static inline void
set_page_ref(struct page *page, int val) {
    page->ref = val;
}

static inline int
page_ref_inc(struct page *page) {
    page->ref += 1;
    return page->ref;
}

static inline int
page_ref_dec(struct page *page) {
    page->ref -= 1;
    return page->ref;
}

pte_t *get_pte(pde_t *pgdir, uintptr_t la, bool create);

void pmm_init(void);

struct page *alloc_pages(int order);
void free_pages(struct page *base, int order);
size_t nr_free_pages(void);

void unmap_range(pde_t *pgdir, uintptr_t start, uintptr_t end);
void exit_range(pde_t *pgdir, uintptr_t start, uintptr_t end);
int copy_range(pde_t *to, pde_t *from, uintptr_t start, uintptr_t end, bool share);

void page_remove(pde_t *pgdir, uintptr_t la);
struct page *pgdir_alloc_page(pde_t *pgdir, uintptr_t la, uint32_t perm);
int page_insert(pde_t *pgdir, struct page *page, uintptr_t la, uint32_t perm);
void tlb_invalidate(pde_t *pgdir, uintptr_t la);

void load_esp0(uintptr_t esp0);

void print_pgdir(void);

#define alloc_page() alloc_pages(0)
#define free_page(page) free_pages(page, 0)

extern char bootstack[], bootstacktop[];

#endif /* __MM_PMM_H__ */