#include "stdio.h"
#include "mm/buddy_pmm.h"

typedef struct {
    list_entry_t free_list[MAX_ORDER+1];      // 管理2的幂次方大小连续物理内存块，下表为order
    unsigned int nr_free[MAX_ORDER+1];
    unsigned int nr_total_free;           // 空闲物理页数量
} free_area_t;

static free_area_t free_area;

// 初始化
static void buddy_init(void) {
    int i;

    for (i = 0; i <= MAX_ORDER; i++) {
        list_init(&(free_area.free_list[i]));
        free_area.nr_free[i] = 0;
    }

    free_area.nr_total_free = 0;
}

static void add_pages(struct page *base, int n, int order) {
    if (order < 0 || order > MAX_ORDER || n <= 0) {
        return;
    }

    int begin = page2ppn(base); // page对应的起始下标
    int end = begin + n; // 结束下标

    // begin向上对齐
    // end向下对齐
    int new_begin = ROUNDUP(begin, 1<<order);
    int new_end = ROUNDDOWN(end, 1<<order);

    // 将page加入对应order的链表中
    int step = 1<<order;
    struct page *p = base + (new_begin-begin);
    for (; p != (base+n-(end-new_end)); p += step) {
        list_add_before(&(free_area.free_list[order]), &(p->page_link));
        p->property = order;
        SetpageProperty(p); // 设置该页为领头页
        free_area.nr_free[order]++;
        free_area.nr_total_free += step;
    }

    p = base;
    for (; p != base+(new_begin-begin); p++) {
        list_add_before(&(free_area.free_list[0]), &(p->page_link));
        p->property = 0;
        SetpageProperty(p);
        free_area.nr_free[0]++;
        free_area.nr_total_free++;
    }

    p = base + n - (end - new_end);
    for (; p != base+n; p++) {
        list_add_before(&(free_area.free_list[0]), &(p->page_link));
        p->property = 0;
        SetpageProperty(p);
        free_area.nr_free[0]++;
        free_area.nr_total_free++; 
    }
}

// 初始化连续内存块
static void buddy_init_memmap(struct page *base, size_t n) {
    // 初始化所有page
    struct page *p = base;
    for (; p != base + n; p++) {
        p->flags = p->property = 0;
        set_page_ref(p, 0);
    }

    add_pages(base, n, MAX_ORDER);
}

static void expend(struct page *page, int want_order, int cur_order) {
    size_t size = 1 << cur_order;

    while (cur_order > want_order) {
        cur_order--;
        size >>= 1;

        list_add_before(&(free_area.free_list[cur_order]), &(page[size].page_link));
        page[size].property = cur_order;
        SetpageProperty(&page[size]);
        free_area.nr_free[cur_order]++;

        free_area.nr_total_free += (1<<cur_order);
    }
}

// 分配物理页函数
static struct page *buddy_alloc_pages(int order) {
    struct page *page = NULL;

    // 找到最小的足够大小的连续内存块
    int cur_order = order;
    for (; cur_order <= MAX_ORDER; cur_order++) {
        if (free_area.nr_free[cur_order] != 0) {
            list_entry_t* list = list_next(&(free_area.free_list[cur_order])); // 得到一个连续内存块
            if (list == &(free_area.free_list[cur_order])) {
                return NULL;
            }

            list_del(list); // 将其从链表中删除
            page = le2page(list, page_link);
            free_area.nr_free[cur_order]--;
            free_area.nr_total_free -= (1<<cur_order);
            break;
        }
    }

    if (!page) {
        return NULL;
    }

    ClearpageProperty(page);

    // 分割
    expend(page, order, cur_order);

    return page;
}

static inline ppn_t find_buddy_pfn(ppn_t page_pfn, int order)
{
	return page_pfn ^ (1 << order);
}

// 释放物理页函数
static void buddy_free_pages(struct page *base, int order) {
    ppn_t ppn = page2ppn(base);
    int cur_order = order;

    while (cur_order < MAX_ORDER) {
        // 找伙伴
        ppn_t buddy_ppn = find_buddy_pfn(ppn, cur_order);
        struct page *buddy_page = base + (buddy_ppn-ppn);

        // 如果这个page是连续内存块的首部，并且它在order链表，那么就可以合并
        if (pageProperty(buddy_page) && (buddy_page->property == cur_order)) {
            list_del(&(buddy_page->page_link)); // 将伙伴从order链表中删除
            ClearpageProperty(buddy_page);
            buddy_page->property = 0;
            free_area.nr_free[cur_order]--;

            cur_order++;
        } else {
            break;
        }
    }

    SetpageProperty(base);
    base->property = cur_order;
    list_add_before(&(free_area.free_list[cur_order]), &(base->page_link));
    free_area.nr_free[cur_order]++;
    free_area.nr_total_free += (1<<cur_order);
}

// 当前空闲的物理页数
static size_t buddy_nr_free_pages(void) {
    return free_area.nr_total_free;
}

static void buddy_dump_area(void) {
    int order;
    for (order = 0; order <= MAX_ORDER; order++) {
        kprint("(%d:%d) ", 1<<order, free_area.nr_free[order]);
    }
    kprint("\ntotal:%d\n", free_area.nr_total_free);
}

const struct pmm_manager buddy_pmm_manager = {
    .name = "buddy_pmm_manager",
    .init = buddy_init,
    .init_memmap = buddy_init_memmap,
    .alloc_pages = buddy_alloc_pages,
    .free_pages = buddy_free_pages,
    .nr_free_pages = buddy_nr_free_pages,
    .dump_area = buddy_dump_area,
};