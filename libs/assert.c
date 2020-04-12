#include "defs.h"
#include "stdio.h"
#include "assert.h"

void spin(char * func)
{
	printk("\nspinning in %s ...\n", func);
	while (1) {}
}

void assertion_failure(char *exp, char *file, char *baseFile, int line)
{
	printk("\nassert(%s) failed: file: %s, base_file: %s, ln%d",
	       exp, file, baseFile, line);

	spin("assertion_failure()");

	// 不可以到达这里
    asm volatile("ud2");
}

void panic(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintk(fmt, ap);
    va_end(ap);
    
    spin("assertion_failure()");

    // 不可以到达这里
	asm volatile("ud2");
}