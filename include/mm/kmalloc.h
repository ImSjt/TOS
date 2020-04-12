#ifndef __MM_KMALLOC_H__
#define __MM_KMALLOC_H__

#include "defs.h"

#define KMALLOC_MAX_ORDER       10

void kmalloc_init(void);

void *kmalloc(size_t n);
void kfree(void *objp);

size_t kallocated(void);

void dump_mm(void);

#endif /* __MM_KMALLOC_H__ */

