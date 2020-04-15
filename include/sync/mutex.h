#ifndef __SYNC_MUTEX_H__
#define __SYNC_MUTEX_H__
#include "sync/wait.h"
#include "process/task.h"

typedef struct {
    uint32_t lock;
    wait_queue_t wait_queue;
} mutex_t;

void mutex_init(mutex_t *mutex);
void mutex_lock(mutex_t *mutex);
void mutex_unlock(mutex_t *mutex);

#endif /* __SYNC_MUTEX_H__ */