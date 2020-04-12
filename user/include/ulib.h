#ifndef __LIBS_ULIB_H__
#define __LIBS_ULIB_H__

#include "defs.h"

void __noreturn __panic(const char *file, int line, const char *fmt, ...);

#define panic(...)                                      \
    __panic(__FILE__, __LINE__, __VA_ARGS__)

#define assert(x)                                       \
    do {                                                \
        if (!(x)) {                                     \
            panic("assertion failed: %s", #x);          \
        }                                               \
    } while (0)

void __noreturn exit(int error_code);
int fork(void);
int wait(void);
int waitpid(int pid, int *store);
void yield(void);
int kill(int pid);
int getpid(void);
void print_pgdir(void);

#endif /* __LIBS_ULIB_H__ */