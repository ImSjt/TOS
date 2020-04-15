#include "unistd.h"
#include "syscall.h"
#include "defs.h"

void sleep(uint32_t s) {
    sys_sleep(s);
}