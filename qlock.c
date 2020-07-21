#include "taskimpl.h"

/* locking
 * 加锁操作先把当前的运行时协程添加到等待队列, 然后检出控制权给调度器去执行其他任务
 * 等有释放锁的协程释放了锁之后, 这里的获取锁协程才有机会获取到锁并且被重新加入调度列表
 */
static int _qlock(QLock *l, int block) {
    /* 这里在判断是不是有人已经持有了锁, 没人持有自己就直接占有, 不需要阻塞 */
    if (l->owner == nil) {
        l->owner = taskrunning;
        return 1;
    }

    if (!block)
        return 0;

    addtask(&l->waiting, taskrunning);
    taskstate("qlock");
    taskswitch();
    if (l->owner != taskrunning) {
        fprint(2, "qlock: owner=%p self=%p oops\n", l->owner, taskrunning);
        abort();
    }

    return 1;
}

void qlock(QLock *l) {
    _qlock(l, 1);
}

int canqlock(QLock *l) {
    return _qlock(l, 0);
}

/* 解锁的时候, 把等待列表里面第一个任务移除并且重归 ready */
void qunlock(QLock *l) {
    Task *ready;

    if (l->owner == 0) {
        fprint(2, "qunlock: owner=0\n");
        abort();
    }

    if ((l->owner = ready = l->waiting.head) != nil) {
        deltask(&l->waiting, ready);
        taskready(ready);
    }
}

/* 读写锁的实现
 * 这里在获取读锁 */
static int _rlock(RWLock *l, int block) {
    /* 要求没有写协程, 并且也没有正在等待获取写锁的协程 */
    if (l->writer == nil && l->wwaiting.head == nil) {
        l->readers++; /* 读锁是共享的 */
        return 1;
    }

    if (!block)
        return 0;

    /* 加入读锁等待者队列 */
    addtask(&l->rwaiting, taskrunning);
    taskstate("rlock");
    taskswitch(); /* 交出自己的 CPU 控制权, 实现阻塞效果 */
    return 1;
}

void rlock(RWLock *l) {
    _rlock(l, 1);
}

int canrlock(RWLock *l) {
    return _rlock(l, 0);
}

/* 获取写锁 */
static int _wlock(RWLock *l, int block) {
    /* 没有写锁, 也没人在读取 */
    if (l->writer == nil && l->readers == 0) {
        l->writer = taskrunning;
        return 1;
    }

    if (!block)
        return 0;

    /* 加入写锁获取等待者队列 */
    addtask(&l->wwaiting, taskrunning);
    taskstate("wlock");
    taskswitch(); /* 交出控制权 */
    return 1;
}

void wlock(RWLock *l) {
    _wlock(l, 1);
}

int canwlock(RWLock *l) {
    return _wlock(l, 0);
}

/* 释放读锁  */
void runlock(RWLock *l) {
    Task *t;

    /* == 0 条件, 说明释放之后, 读锁的持有者数量已经是 0 了
     * 这时候来检查写锁等待者队列, 如果有等待者, 就把锁分给他 */
    if (--l->readers == 0 && (t = l->wwaiting.head) != nil) {
        deltask(&l->wwaiting, t);
        l->writer = t;
        taskready(t);
    }
}

/* 释放写锁 */
void wunlock(RWLock *l) {
    Task *t;

    /* 没有写锁持有者, 一定是其他什么地方出错了 */
    if (l->writer == nil) {
        fprint(2, "wunlock: not locked\n");
        abort();
    }

    l->writer = nil;

    /* 写锁和读锁是互斥的, 刚刚吧写锁释放, 读锁持有者数量应该是 0 才对 */
    if (l->readers != 0) {
        fprint(2, "wunlock: readers\n");
        abort();
    }

    /* 现在让读等待者拿到读锁并且就绪 */
    while ((t = l->rwaiting.head) != nil) {
        deltask(&l->rwaiting, t);
        l->readers++;
        taskready(t);
    }

    /* 写锁等待者开始获取写锁, 注意条件是 readers == 0 */
    if (l->readers == 0 && (t = l->wwaiting.head) != nil) {
        deltask(&l->wwaiting, t);
        l->writer = t;
        taskready(t);
    }
}
