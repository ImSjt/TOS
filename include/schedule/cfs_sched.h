#ifndef __SCHEDULE_CFS_SCHED_H__
#define __SCHEDULE_CFS_SCHED_H__
#include "defs.h"
#include "rb_tree.h"
#include "process/task.h"

extern struct sched_class cfs_sched_class;

struct cfs_run_queue {
    rb_tree *tree;
};

#define rbn2task(node)              \
    (to_struct(node, struct task_struct, rb_link))

#endif /* __SCHEDULE_CFS_SCHED_H__ */
