#include "sync/mutex.h"
#include "atomic.h"
#include "process/task.h"
#include "assert.h"
#include "schedule/sched.h"
#include "sync/wait.h"

void mutex_init(mutex_t *mutex) {
    mutex->lock = 0;
    wait_queue_init(&(mutex->wait_queue));
}

static int _mutex_lock(mutex_t *mutex) {
    bool intr_flag;
    local_intr_save(intr_flag);
    if (mutex->lock == 0) {
        mutex->lock = 1;
        local_intr_restore(intr_flag);
        return 0;
    }
    wait_t __wait, *wait = &__wait;
    wait_current_set(&(mutex->wait_queue), wait, WT_KMUTEX);
    local_intr_restore(intr_flag);

    // 主动调度
    schedule();

    local_intr_save(intr_flag);
    wait_current_del(wait->wait_queue, wait);
    local_intr_restore(intr_flag);

    if (wait->wakeup_flags != WT_KMUTEX) {
        return wait->wakeup_flags;
    }
    return 0;
}

static void _mutex_unlock(mutex_t *mutex) {
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        wait_t *wait;
        if ((wait = wait_queue_first(&(mutex->wait_queue))) == NULL) {
            mutex->lock = 0;
        }
        else {
            assert(wait->task->wait_state == WT_KMUTEX);
            wakeup_wait(wait->wait_queue, wait, WT_KMUTEX, 1);
        }
    }
    local_intr_restore(intr_flag);
}

void mutex_lock(mutex_t *mutex) {
    assert(_mutex_lock(mutex) == 0);
}

void mutex_unlock(mutex_t *mutex) {
    _mutex_unlock(mutex);
}
