#ifndef __KERN_MM_BUDDY_PMM_H__
#define  __KERN_MM_BUDDY_PMM_H__

#include "mm/pmm.h"
#include "mm/memlayout.h"

#define MAX_ORDER 10

extern const struct pmm_manager buddy_pmm_manager;

#endif /* __KERN_MM_BUDDY_PMM_H__ */