#ifndef __SCHEDULE_SCHED_H__
#define __SCHEDULE_SCHED_H__
#include "list.h"
#include "process/task.h"
#include "schedule/default_sched.h"
#include "schedule/o1_sched.h"
#include "schedule/cfs_sched.h"

#define CFS_SCHEDULE

typedef struct {
    unsigned int expires;
    struct task_struct *task;
    list_entry_t timer_link;
} timer_t;

#define le2timer(le, member)            \
to_struct((le), timer_t, member)

static inline timer_t *timer_init(timer_t *timer, struct task_struct *task, int expires) {
    timer->expires = expires;
    timer->task = task;
    list_init(&(timer->timer_link));
    return timer;
}

struct run_queue;

struct sched_class {
    // 调度器的名字
    const char *name;
    // 初始化运行队列
    void (*init)(struct run_queue *rq);
    // 将任务放入运行队列中
    void (*enqueue)(struct run_queue *rq, struct task_struct *task);
    // 从运行队列中取任务
    void (*dequeue)(struct run_queue *rq, struct task_struct *task);
    // 挑选下一个任务
    struct task_struct *(*pick_next)(struct run_queue *rq);
    // 定时调用
    void (*task_tick)(struct run_queue *rq, struct task_struct *proc);
};

struct run_queue {
    struct cfs_run_queue cfs_rq; // cfs调度算法
    struct o1_run_queue o1_rq; // O1调度算法
    struct default_run_queue d_rq; // 默认调度算法
};

void wakeup_proc(struct task_struct *task);
void sched_init(void);
void schedule(void);
void run_timer_list(void);

void sleep(size_t s);

#endif /* __SCHEDULE_SCHED_H__ */