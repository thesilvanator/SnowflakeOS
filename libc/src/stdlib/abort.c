#include <stdio.h>

#ifndef _KERNEL_

#include <stdlib.h>

#endif

__attribute__((__noreturn__))
void abort()
{
#ifdef _KERNEL_
    printf("Kernel Panic: abort()\n");
    while (1) {}
    __builtin_unreachable();
#else
    exit(1);
#endif
}