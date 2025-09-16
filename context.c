/* Copyright (c) 2005-2006 Russ Cox, MIT; see COPYRIGHT */

#include "taskimpl.h"

/**
 * @brief 新建上下文对象
 * 
 * @param ucp 
 * @param func 
 * @param argc 
 * @param ... 
 */
void makecontext(ucontext_t *ucp, void (*func)(void), int argc, ...)
{
    int *sp;

    sp = (int *)ucp->uc_stack.ss_sp + ucp->uc_stack.ss_size / 4;
    sp -= argc;
    sp = (void *)((uintptr_t)sp - (uintptr_t)sp % 16); /* 16-align for OS X */
    memmove(sp, &argc + 1, argc * sizeof(int));

    *--sp = 0; /* return address */
    ucp->uc_mcontext.mc_eip = (long)func;
    ucp->uc_mcontext.mc_esp = (int)sp;
}

int swapcontext(ucontext_t *oucp, const ucontext_t *ucp)
{
    if (getcontext(oucp) == 0)
        setcontext(ucp);
    return 0;
}
