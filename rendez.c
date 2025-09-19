#include "taskimpl.h"

/*
 * sleep and wakeup
 */

/**
 * @brief 协程睡眠等待
 *
 * @param r
 */
void tasksleep(Rendez *r)
{
    addtask(&r->waiting, taskrunning);
    if (r->l)
        qunlock(r->l);

    taskstate("sleep");
    taskswitch();
    if (r->l)
        qlock(r->l);
}

/**
 * @brief 唤醒睡眠等待的协程
 *
 * 如果不指定 all, 只唤醒一个协程后退出, 如果指定了 all, 则将全部的协程都唤醒
 *
 * @param r 汇合点对象
 * @param all 是否唤醒全部任务
 * @return int
 */
static int _taskwakeup(Rendez *r, int all)
{
    int i;
    Task *t;

    for (i = 0;; i++) {
        /* 仅唤醒 i=0 的协程 */
        if (i == 1 && !all)
            break;

        if ((t = r->waiting.head) == nil)
            break;

        deltask(&r->waiting, t); /* deltask 会更新 head 节点*/
        taskready(t);
    }

    return i;
}

/**
 * @brief 唤醒协程(只唤醒一个)
 *
 * @param r 汇合点对象
 * @return int 唤醒的协程个数(始终等于 1)
 */
int taskwakeup(Rendez *r)
{
    return _taskwakeup(r, 0);
}

/**
 * @brief 唤醒协程(全部)
 *
 * @param r 汇合点对象
 * @return int 唤醒的协程个数
 */
int taskwakeupall(Rendez *r)
{
    return _taskwakeup(r, 1);
}
