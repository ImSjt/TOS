#include "assert.h"
#include "schedule/default_sched.h"

static void stride_init(struct run_queue *rq) {
     list_init(&(rq->run_list));
     rq->task_num = 0;
}

static void stride_enqueue(struct run_queue *rq, struct task_struct *task) {
     assert(list_empty(&(task->run_link)));
     list_add_before(&(rq->run_list), &(task->run_link));

     if (task->time_slice == 0 || task->time_slice > rq->max_time_slice) {
          task->time_slice = rq->max_time_slice;
     }
     task->rq = rq;
     rq->task_num++;
}

static void stride_dequeue(struct run_queue *rq, struct task_struct *task) {
     assert(!list_empty(&(task->run_link)) && task->rq == rq);
     list_del_init(&(task->run_link));
     rq->task_num--;
}

static struct task_struct *stride_pick_next(struct run_queue *rq) {
     list_entry_t *le = list_next(&(rq->run_list));

     if (le == &rq->run_list)
          return NULL;
     
     struct task_struct *p = le2task(le, run_link);

     return p;
}

static void stride_proc_tick(struct run_queue *rq, struct task_struct *task) {
     if (task->time_slice > 0) {
          task->time_slice --;
     }
     if (task->time_slice == 0) {
          task->need_resched = 1;
     }
}

struct sched_class default_sched_class = {
     .name = "stride_scheduler",
     .init = stride_init,
     .enqueue = stride_enqueue,
     .dequeue = stride_dequeue,
     .pick_next = stride_pick_next,
     .task_tick = stride_proc_tick,
};