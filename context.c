/* Copyright (c) 2005-2006 Russ Cox, MIT; see COPYRIGHT */

#include "taskimpl.h"

/* 文件做了精简, 只研究 i386 平台上的实现 */

/**
 * @brief 新建上下文对象
 * 
 * @param ucp 保存上下文对象的指针
 * @param func 初始入口函数指针
 * @param argc 入口函数参数数量
 * @param ... 入口函数参数列表
 */
void makecontext(xucontext_t *ucp, void (*func)(void), int argc, ...)
{
    int *sp;

    sp = (int *)ucp->uc_stack.ss_sp + ucp->uc_stack.ss_size / 4;
    sp -= argc; /* 预留给 args 的空间 */
    sp = (void *)((uintptr_t)sp - (uintptr_t)sp % 16); /* 16-align for OS X */
    memmove(sp, &argc + 1, argc * sizeof(int));

    *--sp = 0; /* return address */
    ucp->uc_xmcontext.mc_eip = (long)func;
    ucp->uc_xmcontext.mc_esp = (int)sp;
}

/**
 * @brief 执行运行时上下文切换
 * 
 * @param oucp 要切出的上下文
 * @param ucp 要切入的上下文
 * @return int 
 */
int swapcontext(xucontext_t *oucp, const xucontext_t *ucp)
{
    /* setcontext 实际上永远不会正常返回, 它的返回都是以 ctx 里面保存的 getcontext 时刻的状态返回的
     * 因此 setcontext 返回的时候会导致的 if 语句再次执行一次条件判断
     *
     * 站在 oucp 协程的视角看, getcontext 会有 0/1 两种返回值:
     *  - 返回 0: oucp 交出控制权的时候(相当于本函数参数中 oucp 的地位)
     *  - 返回 1: oucp 接受控制权的时候(相当于本函数参数中  ucp 的地位)
     */
    if (getcontext(oucp) == 0) {
        setcontext(ucp);

        assert(1 == 0); /* never execute */
    }

    return 0;
}
