#include "mm/default_pmm.h"
#include "mm/pmm.h"
#include "string.h"
#include "list.h"
#include "assert.h"

typedef struct {
    list_entry_t free_list;         // the list header
    unsigned int nr_free;           // # of free pages in this free list
} free_area_t;

static free_area_t free_area;

#define free_list (free_area.free_list)
#define nr_free (free_area.nr_free)

// 初始化
static void default_init(void) {
    list_init(&free_list);
    nr_free = 0;
}

// 初始化连续内存块
static void default_init_memmap(struct page *base, size_t n) {
    struct page *p = base;

    // 将连续内存块所有物理页引用计数设置为0
    for (; p != base + n; p ++) {
        p->flags = p->property = 0;
        set_page_ref(p, 0);
    }
    base->property = n; // 表示该连续内存块总共有多少个物理页
    SetpageProperty(base); // 设置该页为领头页
    nr_free += n; // 总的空闲物理页数
    list_add_before(&free_list, &(base->page_link)); // 将该连续内存块添加到free_list进行管理
}

// 分配物理页函数
static struct page *default_alloc_pages(int order) {
    int n = 1 << order;

    // 如果需要的内存大于空闲内存，那么就退出
    if (n > nr_free) {
        return NULL;
    }
    struct page *page = NULL;
    list_entry_t *le = &free_list;
    
    // 找到第一个足够大小的连续内存块
    while ((le = list_next(le)) != &free_list) {
        struct page *p = le2page(le, page_link);
        if (p->property >= n) {
            page = p;
            break;
        }
    }

    // 找到了，将其从free_area中删除
    if (page != NULL) {
        if (page->property > n) {
            struct page *p = page + n;
            p->property = page->property - n;
            SetpageProperty(p);
            list_add_after(&(page->page_link), &(p->page_link));
        }
        list_del(&(page->page_link));
        nr_free -= n;
        ClearpageProperty(page);
    }
    return page;
}

// 释放物理页函数
static void default_free_pages(struct page *base, int order) {
    struct page *p = base;
    size_t n = 1<<order;

    // 将物理页引用计数设置为0
    for (; p != base + n; p ++) {
        p->flags = 0;
        set_page_ref(p, 0);
    }
    base->property = n;
    SetpageProperty(base);
    list_entry_t *le = list_next(&free_list);

    // 将连续的内存块拼接起来
    while (le != &free_list) {
        p = le2page(le, page_link);
        le = list_next(le);
        if (base + base->property == p) {
            base->property += p->property;
            ClearpageProperty(p);
            list_del(&(p->page_link));
        }
        else if (p + p->property == base) {
            p->property += base->property;
            ClearpageProperty(base);
            base = p;
            list_del(&(p->page_link));
        }
    }

    // 将连续内存块插入free_area中
    nr_free += n;
    le = list_next(&free_list);
    while (le != &free_list) {
        p = le2page(le, page_link);
        if (base + base->property <= p) {
            break;
        }
        le = list_next(le);
    }
    list_add_before(le, &(base->page_link));
}

// 当前空闲的物理页数
static size_t default_nr_free_pages(void) {
    return nr_free;
}

void default_dump_area(void) {

}

const struct pmm_manager default_pmm_manager = {
    .name = "default_pmm_manager",
    .init = default_init,
    .init_memmap = default_init_memmap,
    .alloc_pages = default_alloc_pages,
    .free_pages = default_free_pages,
    .nr_free_pages = default_nr_free_pages,
    .dump_area = default_dump_area,
};