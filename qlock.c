#include "taskimpl.h"

/**
 * @brief 获取锁
 *
 * locking
 *
 * @param l 锁对象
 * @param block 是否阻塞等待取到锁
 * @return int 获取结果, 1 表示取得锁, 0 表示取不到锁
 */
static int _qlock(QLock *l, int block)
{
    if (l->owner == nil) {
        l->owner = taskrunning;
        return 1;
    }

    if (!block)
        return 0;
    addtask(&l->waiting, taskrunning);
    taskstate("qlock");

    /* 注意 taskrunning 不在可调度任务列表里面, 下面的 if 条件要成立, 只能是在锁持有者
     * 调用 qunlock 才能重新把 taskrunning 设置 taskready, 进而解除协程的阻塞 */
    taskswitch();
    if (l->owner != taskrunning) {
        fprint(2, "qlock: owner=%p self=%p oops\n", l->owner, taskrunning);
        abort();
    }
    return 1;
}

/**
 * @brief 获取锁
 *
 * @param l 锁对象
 * @return int 获取结果, 1 表示取得锁, 0 表示取不到锁
 */
void qlock(QLock *l)
{
    _qlock(l, 1);
}

/**
 * @brief 尝试获取锁, 如获取不到直接报告无法获取, 不阻塞等待
 *
 * @param l 锁对象
 * @return int 获取结果, 1 表示取得锁, 0 表示取不到锁
 */
int canqlock(QLock *l)
{
    return _qlock(l, 0);
}

/**
 * @brief 释放锁
 *
 * @param l 锁对象
 */
void qunlock(QLock *l)
{
    Task *ready;

    if (l->owner == 0) {
        fprint(2, "qunlock: owner=0\n");
        abort();
    }

    /* 分配锁给新的持有者, 并解除阻塞状态 */
    if ((l->owner = ready = l->waiting.head) != nil) {
        deltask(&l->waiting, ready);
        taskready(ready);
    }
}

/**
 * @brief 获取读写锁(读锁)
 *
 * @param l 锁对象
 * @param block 是否阻塞等待获取锁
 * @return int 成功获取锁返回 1, 否则返回 0
 */
static int _rlock(RWLock *l, int block)
{
    /* 没有写等待, 直接给读者分配锁
     * 有写等待的时候, 把读操作加在等待队列, 等写操作完成读操作才能继续 */
    if (l->writer == nil && l->wwaiting.head == nil) {
        l->readers++;
        return 1;
    }

    if (!block) {
        return 0;
    }

    addtask(&l->rwaiting, taskrunning);
    taskstate("rlock");
    taskswitch();
    return 1;
}

/**
 * @brief 获取读写锁(读锁)
 *
 * @param l 锁对象
 */
void rlock(RWLock *l)
{
    _rlock(l, 1);
}

/**
 * @brief 获取读写锁(读锁), 不阻塞
 *
 * @param l 锁对象
 * @return int 成功获取锁返回 1, 否则返回 0
 */
int canrlock(RWLock *l)
{
    return _rlock(l, 0);
}

/**
 * @brief 取得读写锁(写锁)
 *
 * @param l 锁对象
 * @param block 是否阻塞等待获取锁
 * @return int 成功获取锁返回 1, 否则返回 0
 */
static int _wlock(RWLock *l, int block)
{
    /* 没有写协程, 且没有读者持有锁 */
    if (l->writer == nil && l->readers == 0) {
        l->writer = taskrunning;
        return 1;
    }

    if (!block)
        return 0;

    addtask(&l->wwaiting, taskrunning);
    taskstate("wlock");
    taskswitch();
    return 1;
}

/**
 * @brief 取得读写锁(写锁)
 *
 * @param l 锁对象
 */
void wlock(RWLock *l)
{
    _wlock(l, 1);
}

/**
 * @brief 取得读写锁(写锁), 不阻塞
 *
 * @param l 锁对象
 * @return int 成功获取锁返回 1, 否则返回 0
 */
int canwlock(RWLock *l)
{
    return _wlock(l, 0);
}

/**
 * @brief 释放读锁
 *
 * @param l 锁对象
 */
void runlock(RWLock *l)
{
    Task *t;

    if (--l->readers == 0 && (t = l->wwaiting.head) != nil) {
        deltask(&l->wwaiting, t);
        l->writer = t;
        taskready(t);
    }
}

/**
 * @brief 释放写锁
 *
 * @param l
 */
void wunlock(RWLock *l)
{
    Task *t;

    if (l->writer == nil) {
        fprint(2, "wunlock: not locked\n");
        abort();
    }
    l->writer = nil;

    /* 写锁持有的时候, 不能有读者存在 */
    if (l->readers != 0) {
        fprint(2, "wunlock: readers\n");
        abort();
    }

    /* 调度读者准备执行 */
    while ((t = l->rwaiting.head) != nil) {
        deltask(&l->rwaiting, t);
        l->readers++;
        taskready(t);
    }

    /* 如果现在没有读者, 并且有写者等待, 则调度写者取得锁运行 */
    if (l->readers == 0 && (t = l->wwaiting.head) != nil) {
        deltask(&l->wwaiting, t);
        l->writer = t;
        taskready(t);
    }
}
