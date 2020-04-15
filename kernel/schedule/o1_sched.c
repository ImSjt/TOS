#include "schedule/o1_sched.h"
#include "string.h"
#include "assert.h"
#include "schedule/sched.h"


static int timeslice(int prio) {
    return (MAX_PRIO-prio) * 1;
}

static inline void set_bitmap(uint32_t *bitmap, int i) {
    bitmap[i>>SHIFT] |=  (1<<(i & MASK)); 
}

static inline void clr_bitmap(uint32_t *bitmap, int i) {
    bitmap[i>>SHIFT] &= ~(1<<(i & MASK)); 
}

static inline int test_bitmap(uint32_t *bitmap, int i) { 
    return bitmap[i>>SHIFT] &   (1<<(i & MASK));
}

static inline int get_first_bit(uint32_t *bitmap, size_t size) {
    int i;
    for (i = 0; i < size; i++) {
        if (test_bitmap(bitmap, i))
            return i;
    }

    return -1;
}

static void swap(struct prio_array **p1, struct prio_array **p2) {
    struct prio_array *temp;
    temp = *p1;
    *p1 = *p2;
    *p2 = temp;
}

static void o1_init(struct run_queue *rq) {
    int i, j;
    struct o1_run_queue *o1_rq = &rq->o1_rq;
    struct prio_array *array = o1_rq->array;
    for (i = 0; i < 2; i++) {
        array[i].nr_active = 0;
        memset(array[i].bitmap, 0, sizeof(array[i].bitmap));
        
        for (j = 0; j < MAX_PRIO; j++) {
            list_init(&(array[i].queue[j]));
        }
    }

    o1_rq->active = &array[0];
    o1_rq->expired = &array[1];
}

// 如果超时（时间片为0），那么就加入expired队列
// 如果不超时，则加入active队列
static void enqueue_task(struct prio_array *array, struct task_struct *task) {
    int index = task->prio;
    assert(0 < index && index < MAX_PRIO);

    // 将加成到对应的队列中
    list_add_before(&(array->queue[index]), &(task->run_link));
    set_bitmap(array->bitmap, index);
    array->nr_active++;
}

static void o1_enqueue(struct run_queue *rq, struct task_struct *task) {
    struct o1_run_queue *o1_rq = &(rq->o1_rq);

    // 加入active还是expired
    if (task->time_slice > 0) { // 时间片没有使用完
        enqueue_task(o1_rq->active, task);
    } else if (task->time_slice == 0) { // 新进程
        task->time_slice = timeslice(task->prio);
        enqueue_task(o1_rq->active, task);
    } else { // 时间片已经使用完
        enqueue_task(o1_rq->expired, task);
    }

}

static void o1_dequeue(struct run_queue *rq, struct task_struct *task) {
    
}

static struct task_struct *o1_pick_next(struct run_queue *rq) {
    struct o1_run_queue *o1_rq = &rq->o1_rq;

    if (o1_rq->active->nr_active == 0) {
        swap(&(o1_rq->active), &(o1_rq->expired));
    }

    if (o1_rq->active->nr_active != 0) {
        // 通过搜索bitmap，找到第一个有元素的队列
        int index = get_first_bit(o1_rq->active->bitmap, MAX_PRIO);
        assert(index >= 0 && index < MAX_PRIO);

        list_entry_t *queue = &(o1_rq->active->queue[index]);
        assert(!list_empty(queue));

        list_entry_t *list = list_next(queue);
        list_del_init(list);
        
        if (list_empty(queue)) {
            clr_bitmap(o1_rq->active->bitmap, index);
        }

        o1_rq->active->nr_active--;

        return le2task(list, run_link);
    }

    return NULL;
}

static void o1_proc_tick(struct run_queue *rq, struct task_struct *task) {
     if (task->time_slice >= 0) {
          task->time_slice --;
     }
     if (task->time_slice < 0) {
          task->need_resched = 1;
     }
}

struct sched_class o1_sched_class = {
     .name = "O1_scheduler",
     .init = o1_init,
     .enqueue = o1_enqueue,
     .dequeue = o1_dequeue,
     .pick_next = o1_pick_next,
     .task_tick = o1_proc_tick,
};
