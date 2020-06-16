/* Copyright (c) 2005-2006 Russ Cox, MIT; see COPYRIGHT */

#include <stdio.h>

#include "taskimpl.h"

void makecontext(sucontext_t *ucp, void (*func)(void), int argc, ...) {
    int *sp;

    sp = (int *)ucp->uc_stack.ss_sp + ucp->uc_stack.ss_size / 4;
    sp -= argc;
    sp = (void *)((uintptr_t)sp - (uintptr_t)sp % 16); /* 16-align for OS X */
    memmove(sp, &argc + 1, argc * sizeof(int));

    // printf("AAAAA");

    *--sp = 0; /* return address */
    ucp->uc_mcontext.mc_eip = (long)func;
    ucp->uc_mcontext.mc_esp = (int)sp;
}

int swapcontext(sucontext_t *oucp, const sucontext_t *ucp) {
    if (getcontext(oucp) == 0)
        setcontext(ucp);
    return 0;
}
