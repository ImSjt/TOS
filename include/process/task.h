#ifndef __PROCESS_TASK_H__
#define __PROCESS_TASK_H__
#include "defs.h"
#include "list.h"
#include "trap/trap.h"
#include "mm/memlayout.h"
#include "mm/vmm.h"
#include "schedule/sched.h"

#define TASK_UNINIT 0       // 未初始化
#define TASK_SLEEPING 1     // 睡眠
#define TASK_RUNNABLE 2      // 可运行
#define TASK_ZOMBIE 3       // 僵死

#define PROC_NAME_LEN               50
#define MAX_PROCESS                 4096
#define MAX_PID                     (MAX_PROCESS * 2)

struct task_struct;

extern list_entry_t task_list;
extern struct task_struct *current;
extern struct task_struct *idleproc;
extern struct task_struct *initproc;

struct context {
    uint32_t eip;
    uint32_t esp;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    uint32_t esi;
    uint32_t edi;
    uint32_t ebp;
};

struct task_struct
{
    int state;                                  // 进程状态
    int pid;                                    // PID
    int runs;                                   // 进程运行时间
    uintptr_t kstack;                           // 内核栈
    volatile bool need_resched;                 // 需要重新调度标志
    struct task_struct *parent;                 // 父进程
    struct mm_struct *mm;                       // 进程内存管理
    struct context context;                     // Switch here to run process,thread_info
    struct trapframe *tf;                       // 中断栈帧
    uintptr_t cr3;                              // 页目录表基址
    uint32_t flags;                             // 进程标志
    char name[PROC_NAME_LEN + 1];               // 进程名字
    list_entry_t list_link;                     // 进程链表
    list_entry_t hash_link;                     // 进程哈希链表
    int exit_code;                              // 进程退出码，要传递给父进程
    uint32_t wait_state;                        // 等待状态
    struct task_struct *cptr, *yptr, *optr;     // relations between processes
    struct run_queue *rq;                       // 该进程对应的运行队列
    list_entry_t run_link;                      // 运行队列入口
    int time_slice;                             // 时间片
};

#define PF_EXITING                  0x00000001      // getting shutdown

#define WT_INTERRUPTED               0x80000000                    // the wait state could be interrupted
#define WT_CHILD                    (0x00000001 | WT_INTERRUPTED)  // wait child process
#define WT_KSEM                      0x00000100                    // wait kernel semaphore
#define WT_TIMER                    (0x00000002 | WT_INTERRUPTED)  // wait timer
#define WT_KBD                      (0x00000004 | WT_INTERRUPTED)  // wait the input of keyboard

#define le2task(le, member)         \
    to_struct((le), struct task_struct, member)

void task_init(void);
void proc_run(struct task_struct *task);
int do_exit(int error_code);
int do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf);
int do_wait(int pid, int *code_store);
int do_yield(void);
int do_kill(int pid);
int do_execve(const char *name, size_t len, unsigned char *binary, size_t size);



#endif /* __PROCESS_TASK_H__ */