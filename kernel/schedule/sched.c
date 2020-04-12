#include "stdio.h"
#include "assert.h"
#include "sync/sync.h"
#include "schedule/sched.h"
#include "schedule/default_sched.h"

static list_entry_t timer_list;

static struct sched_class *sched_class;

static struct run_queue *rq;

static inline void sched_class_enqueue(struct task_struct *task) {
    if (task != idleproc) {
        sched_class->enqueue(rq, task);
    }
}

static inline void sched_class_dequeue(struct task_struct *task) {
    sched_class->dequeue(rq, task);
}

static inline struct task_struct *sched_class_pick_next(void) {
    return sched_class->pick_next(rq);
}

static void sched_class_proc_tick(struct task_struct *task) {
    if (task != idleproc) {
        sched_class->task_tick(rq, task);
    }
    else {
        task->need_resched = 1;
    }
}

void wakeup_proc(struct task_struct *task) {
    assert(task->state != TASK_ZOMBIE);
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        if (task->state != TASK_RUNNABLE) {
            task->state = TASK_RUNNABLE;
            task->wait_state = 0;
            if (task != current) {
                sched_class_enqueue(task);
            }
        }
        else {
            printk("wakeup runnable process.\n");
        }
    }
    local_intr_restore(intr_flag);
}

static struct run_queue __rq;

void sched_init(void) {
    list_init(&timer_list);

    sched_class = &default_sched_class;

    rq = &__rq;
    rq->max_time_slice = 5;
    sched_class->init(rq);

    printk("sched class: %s\n", sched_class->name);
}

void schedule(void) {
    bool intr_flag;
    struct task_struct *next;
    local_intr_save(intr_flag);
    {
        current->need_resched = 0;
        if (current->state == TASK_RUNNABLE) {
            sched_class_enqueue(current);
        }
        if ((next = sched_class_pick_next()) != NULL) {
            sched_class_dequeue(next);
        }
        if (next == NULL) {
            next = idleproc;
        }
        next->runs ++;
        if (next != current) {
            proc_run(next);
        }
    }
    local_intr_restore(intr_flag);
}

void add_timer(timer_t *timer) {
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        assert(timer->expires > 0 && timer->task != NULL);
        assert(list_empty(&(timer->timer_link)));
        list_entry_t *le = list_next(&timer_list);
        while (le != &timer_list) {
            timer_t *next = le2timer(le, timer_link);
            if (timer->expires < next->expires) {
                next->expires -= timer->expires;
                break;
            }
            timer->expires -= next->expires;
            le = list_next(le);
        }
        list_add_before(le, &(timer->timer_link));
    }
    local_intr_restore(intr_flag);
}

void del_timer(timer_t *timer) {
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        if (!list_empty(&(timer->timer_link))) {
            if (timer->expires != 0) {
                list_entry_t *le = list_next(&(timer->timer_link));
                if (le != &timer_list) {
                    timer_t *next = le2timer(le, timer_link);
                    next->expires += timer->expires;
                }
            }
            list_del_init(&(timer->timer_link));
        }
    }
    local_intr_restore(intr_flag);
}

void run_timer_list(void) {
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        list_entry_t *le = list_next(&timer_list);
        if (le != &timer_list) {
            timer_t *timer = le2timer(le, timer_link);
            assert(timer->expires != 0);
            timer->expires --;
            while (timer->expires == 0) {
                le = list_next(le);
                struct task_struct *task = timer->task;
                if (task->wait_state != 0) {
                    assert(task->wait_state & WT_INTERRUPTED);
                }
                else {
                    printk("process %d's wait_state == 0.\n", task->pid);
                }
                wakeup_proc(task);
                del_timer(timer);
                if (le == &timer_list) {
                    break;
                }
                timer = le2timer(le, timer_link);
            }
        }
        sched_class_proc_tick(current);
    }
    local_intr_restore(intr_flag);
}