#ifndef __SCHEDULE_DEFAULT_SCHED_H__
#define __SCHEDULE_DEFAULT_SCHED_H__
#include "list.h"

extern struct sched_class default_sched_class;

struct default_run_queue {
    list_entry_t run_list;
    unsigned int task_num;
    int max_time_slice;
};

#endif /* __SCHEDULE_DEFAULT_SCHED_H__ */