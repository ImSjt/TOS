#include "schedule/sched.h"
#include "schedule/cfs_sched.h"

static inline int
cfs_compare(rb_node *node1, rb_node *node2) {
    return rbn2task(node1)->vrun_time - rbn2task(node2)->vrun_time;
}

static void cfs_init(struct run_queue *rq) {
    rq->cfs_rq.tree = rb_tree_create(cfs_compare);
}

static void cfs_enqueue(struct run_queue *rq, struct task_struct *task) {
    rb_insert(rq->cfs_rq.tree, &(task->rb_link));
}

static void cfs_dequeue(struct run_queue *rq, struct task_struct *task) {
    rb_delete(rq->cfs_rq.tree, &(task->rb_link));
}

static struct task_struct *cfs_pick_next(struct run_queue *rq) {
    rb_node *node = rb_node_left_most(rq->cfs_rq.tree);
    if (node == NULL)
        return NULL;

    return rbn2task(node);
}

static void cfs_proc_tick(struct run_queue *rq, struct task_struct *task) {
    task->vrun_time += task->prio;

    rb_node *node = rb_node_left_most(rq->cfs_rq.tree);
    if (node == NULL)
        return;

    struct task_struct *left_most = rbn2task(node);
    if (left_most->vrun_time < task->vrun_time) {
        task->need_resched = 1;
    }
}

struct sched_class cfs_sched_class = {
     .name = "cfs_scheduler",
     .init = cfs_init,
     .enqueue = cfs_enqueue,
     .dequeue = cfs_dequeue,
     .pick_next = cfs_pick_next,
     .task_tick = cfs_proc_tick,
};
