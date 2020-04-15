#ifndef __SCHDULE_O1_SCHED_H__
#define __SCHDULE_O1_SCHED_H__
#include "process/task.h"

#define BITSPERWORD 32
#define SHIFT 5  //2^5 = 32, 移动5个位,左移则相当于乘以32,右移相当于除以32取整 
#define MASK 0x1F  //2^5 = 32, 16进制下的31
#define BITMAP_SIZE (MAX_PRIO/BITSPERWORD + 1)

extern struct sched_class o1_sched_class;

struct prio_array {
    uint32_t nr_active;
    uint32_t bitmap[BITMAP_SIZE];
    list_entry_t queue[MAX_PRIO];
};

struct o1_run_queue {
    struct prio_array *active;
    struct prio_array *expired;
    struct prio_array array[2];
};

#endif /* __SCHDULE_O1_SCHED_H__ */