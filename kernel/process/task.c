#include "stdio.h"
#include "assert.h"
#include "string.h"
#include "unistd.h"
#include "stdlib.h"
#include "error.h"
#include "elf.h"
#include "process/task.h"
#include "mm/pmm.h"
#include "mm/kmalloc.h"
#include "schedule/sched.h"

list_entry_t task_list;

#define HASH_SHIFT          10
#define HASH_LIST_SIZE      (1 << HASH_SHIFT)
#define pid_hashfn(x)       (hash32(x, HASH_SHIFT))

// 进程哈希表
static list_entry_t hash_list[HASH_LIST_SIZE];

// 当前进程
struct task_struct *current = NULL;

// 空闲进程
struct task_struct *idleproc = NULL;

// init进程
struct task_struct *initproc = NULL;

// 总进程的数目
static int nr_task = 0;

void kernel_thread_entry(void);
void forkrets(struct trapframe *tf);
void switch_to(struct context *from, struct context *to);
int do_exit(int error_code);

static struct task_struct *alloc_task(void) {
    struct task_struct *task = kmalloc(sizeof(struct task_struct));
    if (task != NULL) {
        task->state = TASK_UNINIT;
        task->pid = -1;
        task->runs = 0;
        task->kstack = 0;
        task->need_resched = 0;
        task->parent = NULL;
        task->mm = NULL;
        memset(&(task->context), 0, sizeof(struct context));
        task->tf = NULL;
        task->cr3 = boot_cr3;
        task->flags = 0;
        memset(task->name, 0, PROC_NAME_LEN);
        task->wait_state = 0;
        task->cptr = task->optr = task->yptr = NULL;
        task->rq = NULL;
        list_init(&(task->run_link));
        task->time_slice = 0;
        task->prio = MAX_PRIO / 2;
        task->vrun_time = 0;
    }

    return task;
}

char *set_task_name(struct task_struct *task, const char *name) {
    memset(task->name, 0, sizeof(task->name));
    return memcpy(task->name, name, PROC_NAME_LEN);
}

static int setup_kstack(struct task_struct *task) {
    struct page *page = alloc_pages(KSTACKORDER);
    if (task != NULL) {
        task->kstack = (uintptr_t)page2kva(page);
        return 0;
    }
    return -E_NO_MEM;
}

static void put_kstack(struct task_struct *task) {
    free_pages(kva2page((void *)(task->kstack)), KSTACKORDER);
}

static int setup_pgdir(struct mm_struct *mm) {
    struct page *page;
    if ((page = alloc_page()) == NULL) {
        return -E_NO_MEM;
    }
    pde_t *pgdir = page2kva(page);
    memcpy(pgdir, boot_pgdir, PGSIZE);
    pgdir[PDX(VPT)] = PADDR(pgdir) | PTE_P | PTE_W;
    mm->pgdir = pgdir;
    return 0;
}

static void put_pgdir(struct mm_struct *mm) {
    free_page(kva2page(mm->pgdir));
}

static int copy_mm(uint32_t clone_flags, struct task_struct *task) {
    struct mm_struct *mm, *oldmm = current->mm;

    if (oldmm == NULL) {
        return 0;
    }

    if (clone_flags & CLONE_VM) {
        mm = oldmm;
        goto good_mm;
    }

    int ret = -E_NO_MEM;
    if ((mm = mm_create()) == NULL) {
        goto bad_mm;
    }
    if (setup_pgdir(mm) != 0) {
        goto bad_pgdir_cleanup_mm;
    }

    // lock_mm(oldmm);
    {
        ret = dup_mmap(mm, oldmm);
    }
    // unlock_mm(oldmm);

    if (ret != 0) {
        goto bad_dup_cleanup_mmap;
    }

good_mm:
    mm_count_inc(mm);
    task->mm = mm;
    task->cr3 = PADDR(mm->pgdir);
    return 0;
bad_dup_cleanup_mmap:
    exit_mmap(mm);
    put_pgdir(mm);
bad_pgdir_cleanup_mm:
    mm_destroy(mm);
bad_mm:
    return ret;
}

static void
forkret(void) {
    forkrets(current->tf);
}

static void copy_thread(struct task_struct *task, uintptr_t esp, struct trapframe *tf) {
    task->tf = (struct trapframe *)(task->kstack + KSTACKSIZE) - 1;
    *(task->tf) = *tf;
    task->tf->tf_regs.reg_eax = 0;
    task->tf->tf_esp = esp;
    task->tf->tf_eflags |= FL_IF;

    task->context.eip = (uintptr_t)forkret;
    task->context.esp = (uintptr_t)(task->tf);
}

static void
hash_proc(struct task_struct *task) {
    list_add(hash_list + pid_hashfn(task->pid), &(task->hash_link));
}

static void
unhash_proc(struct task_struct *task) {
    list_del(&(task->hash_link));
}

struct task_struct *find_task(int pid) {
    if (0 < pid && pid < MAX_PID) {
        list_entry_t *list = hash_list + pid_hashfn(pid), *le = list;
        while ((le = list_next(le)) != list) {
            struct task_struct *task = le2task(le, hash_link);
            if (task->pid == pid) {
                return task;
            }
        }
    }
    return NULL;
}

static void
set_links(struct task_struct *task) {
    list_add(&task_list, &(task->list_link));
    task->yptr = NULL;
    if ((task->optr = task->parent->cptr) != NULL) {
        task->optr->yptr = task;
    }
    task->parent->cptr = task;
    nr_task ++;
}

static void
remove_links(struct task_struct *task) {
    list_del(&(task->list_link));
    if (task->optr != NULL) {
        task->optr->yptr = task->yptr;
    }
    if (task->yptr != NULL) {
        task->yptr->optr = task->optr;
    }
    else {
       task->parent->cptr = task->optr;
    }
    nr_task --;
}

static int get_pid(void) {
    struct task_struct *task;
    list_entry_t *list = &task_list, *le;
    static int next_safe = MAX_PID, last_pid = MAX_PID;
    if (++last_pid >= MAX_PID) {
        last_pid = 1;
        goto inside;
    }
    if (last_pid >= next_safe) {
    inside:
        next_safe = MAX_PID;
    repeat:
        le = list;
        while ((le = list_next(le)) != list) {
            task = le2task(le, list_link);
            if (task->pid == last_pid) {
                if (++last_pid >= next_safe) {
                    if (last_pid >= MAX_PID) {
                        last_pid = 1;
                    }
                    next_safe = MAX_PID;
                    goto repeat;
                }
            }
            else if (task->pid > last_pid && next_safe > task->pid) {
                next_safe = task->pid;
            }
        }
    }
    return last_pid;
}

void proc_run(struct task_struct *task) {
    if (task != current) {
        bool intr_flag;
        struct task_struct *prev = current, *next = task;
        local_intr_save(intr_flag);
        {
            current = task;
            load_esp0(next->kstack + KSTACKSIZE);
            lcr3(next->cr3);
            switch_to(&(prev->context), &(next->context));
        }
        local_intr_restore(intr_flag);
    }
}

int do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf) {
    int ret = -E_NO_FREE_PROC;
    struct task_struct *task;
    if (nr_task >= MAX_PROCESS) {
        goto fork_out;
    }
    ret = -E_NO_MEM;

    if ((task = alloc_task()) == NULL) {
        goto fork_out;
    }

    task->parent = current;
    assert(current->wait_state == 0);

    if (setup_kstack(task) != 0) {
        goto bad_fork_cleanup_task;
    }

    if (copy_mm(clone_flags, task) != 0) {
        goto bad_fork_cleanup_fs;
    }

    copy_thread(task, stack, tf);

    bool intr_flag;
    local_intr_save(intr_flag);
    {
        task->pid = get_pid();
        hash_proc(task);
        set_links(task);
    }
    local_intr_restore(intr_flag);

    wakeup_proc(task);

    ret = task->pid;
fork_out:
    return ret;

bad_fork_cleanup_fs:
    // put_fs(proc);
// bad_fork_cleanup_kstack:
    put_kstack(task);
bad_fork_cleanup_task:
    kfree(task);
    goto fork_out;
}

int kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags) {
    struct trapframe tf;
    memset(&tf, 0, sizeof(struct trapframe));
    tf.tf_cs = KERNEL_CS;
    tf.tf_ds = tf.tf_es = tf.tf_ss = KERNEL_DS;
    tf.tf_regs.reg_ebx = (uint32_t)fn;
    tf.tf_regs.reg_edx = (uint32_t)arg;
    tf.tf_eip = (uint32_t)kernel_thread_entry;
    return do_fork(clone_flags | CLONE_VM, 0, &tf);
}

int do_wait(int pid, int *code_store) {
    struct mm_struct *mm = current->mm;
    if (code_store != NULL) {
        if (!user_mem_check(mm, (uintptr_t)code_store, sizeof(int), 1)) {
            return -E_INVAL;
        }
    }

    struct task_struct *task;
    bool intr_flag, haskid;
repeat:
    haskid = 0;
    if (pid != 0) {
        task = find_task(pid);
        if (task != NULL && task->parent == current) {
            haskid = 1;
            if (task->state == TASK_ZOMBIE) {
                goto found;
            }
        }
    }
    else {
        task = current->cptr;
        for (; task != NULL; task = task->optr) {
            haskid = 1;
            if (task->state == TASK_ZOMBIE) {
                goto found;
            }
        }
    }
    if (haskid) {
        current->state = TASK_SLEEPING;
        current->wait_state = WT_CHILD;
        schedule();
        if (current->flags & PF_EXITING) {
            do_exit(-E_KILLED);
        }
        goto repeat;
    }
    return -E_BAD_PROC;

found:
    if (task == idleproc || task == initproc) {
        panic("wait idleproc or initproc.\n");
    }
    if (code_store != NULL) {
        *code_store = task->exit_code;
    }
    local_intr_save(intr_flag);
    {
        unhash_proc(task);
        remove_links(task);
    }
    local_intr_restore(intr_flag);
    put_kstack(task);
    kfree(task);
    return 0;
}

int do_yield(void) {
    current->need_resched = 1;
    return 0;
}

int do_kill(int pid) {
    struct task_struct *task;
    if ((task = find_task(pid)) != NULL) {
        if (!(task->flags & PF_EXITING)) {
            task->flags |= PF_EXITING;
            if (task->wait_state & WT_INTERRUPTED) {
                wakeup_proc(task);
            }
            return 0;
        }
        return -E_KILLED;
    }
    return -E_INVAL;
}

static int kernel_execve(const char *name, unsigned char *binary, size_t size) {
    int ret, len = strlen(name);
    asm volatile ( 
        "int %1;"
        : "=a" (ret)
        : "i" (T_SYSCALL), "a" (SYS_exec), "d" (name), "c" (len), "b" (binary), "D" (size)
        : "memory");
    return ret;
}

#define __KERNEL_EXECVE(name, binary, size) ({                          \
            printk("kernel_execve: pid = %d, name = \"%s\".\n",        \
                    current->pid, name);                                \
            kernel_execve(name, binary, (size_t)(size));                \
        })

#define KERNEL_EXECVE(x) ({                                             \
            extern unsigned char _binary_user___user_##x##_out_start[],  \
                _binary_user___user_##x##_out_size[];                    \
            __KERNEL_EXECVE(#x, _binary_user___user_##x##_out_start,     \
                            _binary_user___user_##x##_out_size);         \
        })

static int user_main(void *arg) {
    KERNEL_EXECVE(exit);

    panic("user_main execve failed.\n");

    return 0;
}

// init进程
static int init_main(void *arg) {
    // int ret;
    
    size_t nr_free_pages_store = nr_free_pages();
    size_t kernel_allocated_store = kallocated();

    // 启动一个内核线程
    int pid = kernel_thread(user_main, NULL, 0);
    if (pid <= 0) {
        panic("create user_main failed.\n");
    }

    while (do_wait(0, NULL) == 0) {
        schedule();
    }

    // fs_cleanup();

    printk("all user-mode processes have quit.\n");
    assert(initproc->cptr == NULL && initproc->yptr == NULL && initproc->optr == NULL);
    assert(nr_task == 2);
    assert(list_next(&task_list) == &(initproc->list_link));
    assert(list_prev(&task_list) == &(initproc->list_link));
    assert(nr_free_pages_store == nr_free_pages());
    assert(kernel_allocated_store == kallocated());
    printk("init check memory pass.\n");
    return 0;
}

void task_init(void) {
    int i;

    list_init(&task_list);
    
    for (i = 0; i < HASH_LIST_SIZE; i ++) {
        list_init(hash_list + i);
    }
    
    if ((idleproc = alloc_task()) == NULL) {
        panic("cannot alloc idleproc.\n");
    }

    idleproc->pid = 0;
    idleproc->state = TASK_RUNNABLE;
    idleproc->kstack = (uintptr_t)bootstack;
    idleproc->need_resched = 1;

    set_task_name(idleproc, "idle");
    nr_task ++;

    current = idleproc;

    int pid = kernel_thread(init_main, NULL, 0);
    if (pid <= 0) {
        panic("create init_main failed.\n");
    }

    initproc = find_task(pid);
    set_task_name(initproc, "init");

    assert(idleproc != NULL && idleproc->pid == 0);
    assert(initproc != NULL && initproc->pid == 1);
}

int do_exit(int error_code) {
    printk("--------------------%s\n", current->name);

    if (current == idleproc) {
        panic("idleproc exit.\n");
    }
    if (current == initproc) {
        panic("initproc exit.\n");
    }
    
    struct mm_struct *mm = current->mm;
    if (mm != NULL) {
        lcr3(boot_cr3);
        if (mm_count_dec(mm) == 0) {
            exit_mmap(mm);
            put_pgdir(mm);
            mm_destroy(mm);
        }
        current->mm = NULL;
    }
    current->state = TASK_ZOMBIE;
    current->exit_code = error_code;
    
    bool intr_flag;
    struct task_struct *task;
    local_intr_save(intr_flag);
    {
        task = current->parent;
        if (task->wait_state == WT_CHILD) {
            wakeup_proc(task);
        }
        while (current->cptr != NULL) {
            task = current->cptr;
            current->cptr = task->optr;
    
            task->yptr = NULL;
            if ((task->optr = initproc->cptr) != NULL) {
                initproc->cptr->yptr = task;
            }
            task->parent = initproc;
            initproc->cptr = task;
            if (task->state == TASK_ZOMBIE) {
                if (initproc->wait_state == WT_CHILD) {
                    wakeup_proc(initproc);
                }
            }
        }
    }
    local_intr_restore(intr_flag);
    
    schedule();
    panic("do_exit will not return!! %d.\n", current->pid);

    return 0;
}

static int load_icode(unsigned char *binary, size_t size) {
    if (current->mm != NULL) {
        panic("load_icode: current->mm must be empty.\n");
    }

    int ret = -E_NO_MEM;
    struct mm_struct *mm;
    // 1.分配一个mm_struct结构体
    if ((mm = mm_create()) == NULL) {
        goto bad_mm;
    }
    // 2.分配页目录表，并且自映射
    if (setup_pgdir(mm) != 0) {
        goto bad_pgdir_cleanup_mm;
    }

    // 3.开始解析elf格式文件
    struct page *page = NULL;
    // 3.1.获得elf头部
    struct elfhdr *elf = (struct elfhdr *)binary;
    // 3.2.获得段
    struct proghdr *ph = (struct proghdr *)(binary + elf->e_phoff); // 段入口
    // 3.3.判断是否有效
    if (elf->e_magic != ELF_MAGIC) {
        ret = -E_INVAL_ELF;
        goto bad_elf_cleanup_pgdir;
    }

    uint32_t vm_flags, perm;
    struct proghdr *ph_end = ph + elf->e_phnum; // 段结束
    // 3.4.遍历所有的段，分配虚拟内存，分配物理内存，映射并拷贝
    for (; ph < ph_end; ph ++) {
        if (ph->p_type != ELF_PT_LOAD) {
            continue ;
        }
        if (ph->p_filesz > ph->p_memsz) {
            ret = -E_INVAL_ELF;
            goto bad_cleanup_mmap;
        }
        if (ph->p_filesz == 0) {
            continue ;
        }

        // 3.5.设计虚拟内存
        vm_flags = 0, perm = PTE_U;
        if (ph->p_flags & ELF_PF_X) vm_flags |= VM_EXEC;
        if (ph->p_flags & ELF_PF_W) vm_flags |= VM_WRITE;
        if (ph->p_flags & ELF_PF_R) vm_flags |= VM_READ;
        if (vm_flags & VM_WRITE) perm |= PTE_W;
        if ((ret = mm_map(mm, ph->p_va, ph->p_memsz, vm_flags, NULL)) != 0) { // 分配虚拟内存
            goto bad_cleanup_mmap;
        }
        unsigned char *from = binary + ph->p_offset; // 获取段在文件中的位置
        size_t off, size;
        uintptr_t start = ph->p_va, end, la = ROUNDDOWN(start, PGSIZE); // 虚拟地址

        ret = -E_NO_MEM;

        // 3.6.分配内存，设置页表，拷贝段
        end = ph->p_va + ph->p_filesz;
        // 3.6.1.拷贝文本/数据段
        while (start < end) {
            // 分配物理页，并设置好映射
            if ((page = pgdir_alloc_page(mm->pgdir, la, perm)) == NULL) {
                goto bad_cleanup_mmap;
            }
            off = start - la, size = PGSIZE - off, la += PGSIZE;
            if (end < la) {
                size -= la - end;
            }

            // 将elf中的段拷贝到对应的虚拟内存
            memcpy(page2kva(page) + off, from, size);
            start += size, from += size;
        }

        // 3.6.2.清空bss段
        end = ph->p_va + ph->p_memsz;
        if (start < la) {
            /* ph->p_memsz == ph->p_filesz */
            if (start == end) {
                continue ;
            }
            off = start + PGSIZE - la, size = PGSIZE - off;
            if (end < la) {
                size -= la - end;
            }
            memset(page2kva(page) + off, 0, size);
            start += size;
            assert((end < la && start == end) || (end >= la && start == la));
        }
        while (start < end) {
            if ((page = pgdir_alloc_page(mm->pgdir, la, perm)) == NULL) {
                goto bad_cleanup_mmap;
            }
            off = start - la, size = PGSIZE - off, la += PGSIZE;
            if (end < la) {
                size -= la - end;
            }
            memset(page2kva(page) + off, 0, size);
            start += size;
        }
    }

    // 4.设置用户栈
    vm_flags = VM_READ | VM_WRITE | VM_STACK;
    if ((ret = mm_map(mm, USTACKTOP - USTACKSIZE, USTACKSIZE, vm_flags, NULL)) != 0) {
        goto bad_cleanup_mmap;
    }
    assert(pgdir_alloc_page(mm->pgdir, USTACKTOP-PGSIZE , PTE_USER) != NULL);
    assert(pgdir_alloc_page(mm->pgdir, USTACKTOP-2*PGSIZE , PTE_USER) != NULL);
    assert(pgdir_alloc_page(mm->pgdir, USTACKTOP-3*PGSIZE , PTE_USER) != NULL);
    assert(pgdir_alloc_page(mm->pgdir, USTACKTOP-4*PGSIZE , PTE_USER) != NULL);
    
    // 5.设置cr3（页目录表基址）
    mm_count_inc(mm);
    current->mm = mm;
    current->cr3 = PADDR(mm->pgdir);
    lcr3(PADDR(mm->pgdir)); // 重新加载页表

    // 6.设置栈帧
    struct trapframe *tf = current->tf;
    memset(tf, 0, sizeof(struct trapframe));
    tf->tf_cs = USER_CS;
    tf->tf_ds = tf->tf_es = tf->tf_ss = USER_DS;
    tf->tf_esp = USTACKTOP;
    tf->tf_eip = elf->e_entry;
    tf->tf_eflags = FL_IF;
    ret = 0;
out:
    return ret;
bad_cleanup_mmap:
    exit_mmap(mm);
bad_elf_cleanup_pgdir:
    put_pgdir(mm);
bad_pgdir_cleanup_mm:
    mm_destroy(mm);
bad_mm:
    goto out;
}

int do_execve(const char *name, size_t len, unsigned char *binary, size_t size) {
    struct mm_struct *mm = current->mm;
    if (!user_mem_check(mm, (uintptr_t)name, len, 0)) {
        return -E_INVAL;
    }
    if (len > PROC_NAME_LEN) {
        len = PROC_NAME_LEN;
    }

    char local_name[PROC_NAME_LEN + 1];
    memset(local_name, 0, sizeof(local_name));
    memcpy(local_name, name, len);

    // 如果mm不为空，则需要释放原先的内存
    if (mm != NULL) {
        lcr3(boot_cr3); // 加载内核的页表
        // 释放内存
        if (mm_count_dec(mm) == 0) {
            exit_mmap(mm); // 释放物理页，释放页表
            put_pgdir(mm); // 释放页目录表
            mm_destroy(mm); // 释放mm_struct
        }
        current->mm = NULL;
    }

    // 加载二进制文件
    int ret;
    if ((ret = load_icode(binary, size)) != 0) {
        goto execve_exit;
    }
    set_task_name(current, local_name);
    return 0;

execve_exit:
    do_exit(ret);
    panic("already exit: %e.\n", ret);

    return 0;
}
