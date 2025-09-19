/* Copyright (c) 2005 Russ Cox, MIT; see COPYRIGHT */

#include "taskimpl.h"

#include <fcntl.h>
#include <stdio.h>

/* main 函数的 argc/argv 是通过下面两个全局变量, 传递到 taskmainstart 逻辑里面的
 * 也只有 taskmainstart 需要这两个参数, 正常的其他 task 接受的参数是一个 void 指针 */

static int taskargc;
static char **taskargv;
int mainstacksize;

static int taskidgen;

/* ------ */

int taskdebuglevel;
int taskcount; /* 记录目前有多少个 "非系统任务" 协程, 这个数目也不包含调度器协程 */
int tasknswitch;   /* 记录当前已经做了多少次协程切换操作 */
int taskexitval;   /* 当前正在运行中协程退出码 */
Task *taskrunning; /* 指向当前正在运行的协程对象 */

Context taskschedcontext; /* 调度器上下文 */
Tasklist taskrunqueue;    /* 待运行协程队列 */

Task **alltask;
int nalltask;

static char *argv0;
static void contextswitch(Context *from, Context *to);

/**
 * @brief 协程信息 debug
 *
 * @param fmt
 * @param ...
 */
static void taskdebug(char *fmt, ...)
{
    va_list arg;
    char buf[128];
    Task *t;
    char *p;
    static int fd = -1;

    return;
    va_start(arg, fmt);
    vfprint(1, fmt, arg);
    va_end(arg);
    return;

    if (fd < 0) {
        p = strrchr(argv0, '/');
        if (p)
            p++;
        else
            p = argv0;
        snprint(buf, sizeof buf, "/tmp/%s.tlog", p);
        if ((fd = open(buf, O_CREAT | O_WRONLY, 0666)) < 0)
            fd = open("/dev/null", O_WRONLY);
    }

    va_start(arg, fmt);
    vsnprint(buf, sizeof buf, fmt, arg);
    va_end(arg);
    t = taskrunning;
    if (t)
        fprint(fd, "%d.%d: %s\n", getpid(), t->id, buf);
    else
        fprint(fd, "%d._: %s\n", getpid(), buf);
}

/**
 * @brief 启动任务
 *
 * @param y 被启动的 Task 地址低 32 bits
 * @param x 被启动的 Task 地址高 32 bits
 */
static void taskstart(uint y, uint x)
{
    Task *t;
    ulong z;

    z = x << 16; /* hide undefined 32-bit shift from 32-bit compilers */
    z <<= 16;
    z |= y;
    t = (Task *)z;

    //print("taskstart %p\n", t);
    t->startfn(t->startarg);
    //print("taskexits %p\n", t);
    taskexit(0);
    //print("not reacehd\n");
}

/**
 * @brief 创建一个协程对象
 *
 * @param fn 入口函数
 * @param arg 入口函数参数
 * @param stack 运行时栈大小
 * @return Task* 协程对象
 */
static Task *taskalloc(void (*fn)(void *), void *arg, uint stack)
{
    Task *t;
    sigset_t zero;
    uint x, y;
    ulong z;

    /* allocate the task and stack together */
    t = malloc(sizeof *t + stack);
    if (t == nil) {
        fprint(2, "taskalloc malloc: %r\n");
        abort();
    }

    memset(t, 0, sizeof *t);

    t->stk = (uchar *)(t + 1); /* 设置栈指针 */
    t->stksize = stack;        /* 运行时栈大小 */
    t->id = ++taskidgen;       /* 协程 id */

    /* 入口函数与函数参数 */
    t->startfn = fn;
    t->startarg = arg;

    /* do a reasonable initialization
     * TODO: 研究一下这块信号处理 */
    memset(&t->context.uc, 0, sizeof t->context.uc);
    sigemptyset(&zero);
    sigprocmask(SIG_BLOCK, &zero, &t->context.uc.uc_sigmask);

    /* must initialize with current context
     * 初始化 context 对象 */
    if (getcontext(&t->context.uc) < 0) {
        fprint(2, "getcontext: %r\n");
        abort();
    }

    /* call makecontext to do the real work.
     * leave a few words open on both ends
     *
     * TODO: 仔细研究下
     * 答:
     *
     * uc_stack 是用于给信号处理设置单独的栈, 主要是为了防止将来栈内存占满了触发
     * SIGSEG 的时候没有足够的栈空间来处理信号 */
    t->context.uc.uc_stack.ss_sp = t->stk + 8;
    t->context.uc.uc_stack.ss_size = t->stksize - 64;

    /*
     * All this magic is because you have to pass makecontext a
     * function that takes some number of word-sized variables,
     * and on 64-bit machines pointers are bigger than words.
     *
     * 下面一通计算, y 保留 t 的低 32bits, x 保留 t 的高 32bits
     */
    //print("make %p\n", t);
    z = (ulong)t;
    y = z;
    z >>= 16; /* hide undefined 32-bit shift from 32-bit compilers */
    x = z >> 16;

    makecontext(&t->context.uc, (void (*)())taskstart, 2, y, x);

    return t;
}

/**
 * @brief 创建协程
 *
 * @param fn 入口函数
 * @param arg 函数的参数
 * @param stack 函数的栈大小
 * @return int
 */
int taskcreate(void (*fn)(void *), void *arg, uint stack)
{
    int id;
    Task *t;

    t = taskalloc(fn, arg, stack);
    taskcount++;
    id = t->id;

    /* 所有任务都在 alltask 上面记录
     * 然后用 `t->alltaskslot` 记录在 alltask 里面的索引号 */
    if (nalltask % 64 == 0) {
        alltask = realloc(alltask, (nalltask + 64) * sizeof(alltask[0]));
        if (alltask == nil) {
            fprint(2, "out of memory\n");
            abort();
        }
    }

    t->alltaskslot = nalltask;
    alltask[nalltask++] = t;
    taskready(t);
    return id;
}

/**
 * @brief 标记某个任务为系统级
 *
 * Mark the current task as a "system" task.  These are ignored
 * for the purposes of deciding the program is done running
 * (see taskexit next).
 */
void tasksystem(void)
{
    if (!taskrunning->system) {
        taskrunning->system = 1;
        --taskcount;
    }
}

/**
 * @brief 切换到调度器协程运行
 */
void taskswitch(void)
{
    needstack(0);
    contextswitch(&taskrunning->context, &taskschedcontext);
}

/**
 * @brief 置任务为可调度状态
 *
 * 将任务状态置位可调度, 并把任务加到任务队列里面
 *
 * @param t
 */
void taskready(Task *t)
{
    t->ready = 1;
    addtask(&taskrunqueue, t);
}

/**
 * @brief 主动交出 CPU
 *
 * 返回值 -1 表示让出去资源之后没有做任何调度又回到这里来了
 * 这种情况不会发生因为我们的 taskrunqueue 队列里面最少有一个当前运行的 taskrunning
 *
 * 本函数返回此次 yield 之后, 到再度重新被执行, 经历了多少次任务切换
 * 如果只有这个任务自己在被调度, 返回值应该是 0 */
int taskyield(void)
{
    int n;

    n = tasknswitch;

    /* 先把当前任务放回到调度队列 */
    taskready(taskrunning);

    /* 然后更新任务状态 */
    taskstate("yield");

    /* 切换到其他任务执行 */
    taskswitch();

    return tasknswitch - n - 1;
}

/**
 * @brief 检查调度队列是否为空
 *
 * @return int 0-空, 1-非空
 */
int anyready(void)
{
    return taskrunqueue.head != nil;
}

/**
 * @brief 退出程序
 *
 * @param val
 */
void taskexitall(int val)
{
    exit(val);
}

/**
 * @brief 退出任务
 *
 * Exit the current task.  If this is the last non-system task,
 * exit the entire program using the given exit status.
 *
 * @param val
 */
void taskexit(int val)
{
    taskexitval = val;
    taskrunning->exiting = 1;
    taskswitch();
}

/**
 * @brief 切换任务
 *
 * @param from 即将被切出的任务
 * @param to 即将被切入的任务
 */
static void contextswitch(Context *from, Context *to)
{
    if (swapcontext(&from->uc, &to->uc) < 0) {
        fprint(2, "swapcontext failed: %r\n");
        assert(0);
    }
}

/**
 * @brief 协程调度器
 *
 * 这个函数实际上不在定义好的 Task 里面执行(为描述方便, 把这个函数的执行流程叫做调度器协程),
 * 所有的协程间任务切换, 都是从 `调度器协程->自定义 task ->调度器协程` 这样处理的
 */
static void taskscheduler(void)
{
    int i;
    Task *t;

    taskdebug("scheduler enter");

    for (;;) {
        if (taskcount == 0) {
            /* 当最后一个 non-system 任务退出之后, 这个程序随之退出 */
            exit(taskexitval);
        }

        t = taskrunqueue.head;
        if (t == nil) {
            fprint(2, "no runnable tasks! %d tasks stalled\n", taskcount);
            exit(1);
        }

        deltask(&taskrunqueue, t);
        t->ready = 0;
        taskrunning = t;
        tasknswitch++; /* 协程切换统计计数 */
        taskdebug("run %d (%s)", t->id, t->name);

        /* 切换任务, 从调度器切换到具体的协程 */
        contextswitch(&taskschedcontext, &t->context);

        //print("back in scheduler\n");
        taskrunning = nil;

        /* 协程已经退出, 清理 */
        if (t->exiting) {
            if (!t->system) {
                taskcount--;
            }

            /* 把倒数第一个任务, 移动到现在要被删除的这个任务位置上来 */
            i = t->alltaskslot;
            alltask[i] = alltask[--nalltask];
            alltask[i]->alltaskslot = i;
            free(t);
        }
    }
}

/**
 * @brief 获取协程附带的用户数据
 *
 * @return void**
 */
void **taskdata(void)
{
    return &taskrunning->udata;
}

/**
 * @brief 构造协程名称字符串
 *
 * debugging
 *
 * @param fmt
 * @param ...
 */
void taskname(char *fmt, ...)
{
    va_list arg;
    Task *t;

    t = taskrunning;
    va_start(arg, fmt);
    vsnprint(t->name, sizeof t->name, fmt, arg);
    va_end(arg);
}

/**
 * @brief 获取协程名称
 *
 * @return char*
 */
char *taskgetname(void)
{
    return taskrunning->name;
}

/**
 * @brief 构造运行状态字符串
 *
 * @param fmt
 * @param ...
 */
void taskstate(char *fmt, ...)
{
    va_list arg;
    Task *t;

    t = taskrunning;
    va_start(arg, fmt);
    vsnprint(t->state, sizeof t->name, fmt, arg);
    va_end(arg);
}

/**
 * @brief 获取协程运行状态
 *
 * @return char*
 */
char *taskgetstate(void)
{
    return taskrunning->state;
}

/**
 * @brief 检查栈是否够用
 *
 * @param n 所需大小
 */
void needstack(int n)
{
    Task *t;

    t = taskrunning;

    char *at = (char *)&t; /* 这里取局部变量 t 的地址, 这个地址在栈上 */
    char *stk = (char *)t->stk;

    /* 一个 task 结构的内存如下: `开始(Low)--task 结构(也是stk指向)--栈内存--结束(High)`
     *
     * 1. at < stk 的话, 说明栈已经溢出以 stk 为栈边界的内存范围, 侵入到 task 数据结构体范围内了
     * 2. at 和 stk 间距离过小(小于 256), 有栈溢出的风险, 也阻止执行 */
    if (at <= stk || at - stk < 256 + n) {
        fprint(2, "task stack overflow: &t=%p tstk=%p n=%d\n", &t, t->stk, 256 + n);
        abort();
    }
}

/**
 * @brief 往标准错误输出全部协程的信息
 *
 * 可以使用 `Ctrl+|` 或者 `kill -QUIT pid` 触发此函数的打印
 *
 * @param s
 */
static void taskinfo(int s)
{
    char buf[128];
    int i;
    Task *t;
    char *extra;

    fprint(2, "task list:\n");
    fprint(2, "-------------------------------------------------------------------\n");
    fprint(2, "%-6s\t%-15s\t%-25s\t%-15s\n", "TaskID", "TaskName", "State", "Extra");
    fprint(2, "-------------------------------------------------------------------\n");

    for (i = 0; i < nalltask; i++) {
        t = alltask[i];

        if (t == taskrunning) {
            extra = "(running)";
        } else if (t->ready) {
            extra = "(ready)";
        } else {
            extra = "-";
        }

        sprintf(buf, "%d%c", t->id, t->system ? 's' : ' ');
        fprint(2, "%-6s\t%-15s\t%-25s\t%-15s\n", buf, t->name, t->state, extra);
    }
}

/*
 * startup
 */

/**
 * @brief 用户编写的 `taskmain` 函数对应的协程包装
 *
 * @param v
 */
static void taskmainstart(void *v)
{
    taskname("taskmain");
    taskmain(taskargc, taskargv);
}

/**
 * @brief 所有使用了 libtask 库的引用, 统一的入口
 *
 * 在本函数里面会把用户编写的 taskmain 函数, 整理成一个 task, 然后启动调度器
 * 调度执行这个 task. 一般来说用户会在 taskmain 里面在创建更多的 task, 这样
 * 整个系统就可以运行起来了
 *
 * @param argc
 * @param argv
 * @return int
 */
int main(int argc, char **argv)
{
    /* TODO: 了解一下这块的信号管理是怎么做的 */
    struct sigaction sa, osa;

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = taskinfo; /* 利用信号来触发 taskinfo 函数, 输出协程的相关信息 */
    sa.sa_flags = SA_RESTART;
    sigaction(SIGQUIT, &sa, &osa);

#ifdef SIGINFO
    sigaction(SIGINFO, &sa, &osa);
#endif

    /* 程序名字, libtask 是一个库, 这个程序名字是编译成二进制之后用户写的那个程序名字 */
    argv0 = argv[0];

    taskargc = argc;
    taskargv = argv;

    if (mainstacksize == 0)
        mainstacksize = 256 * 1024;

    taskcreate(taskmainstart, nil, mainstacksize); /* 创建主协程对象 */
    taskscheduler();                               /* 开始调度协程执行 */

    fprint(2, "taskscheduler returned in main!\n");
    abort();
    return 0;
}

/**
 * @brief 向链表追加节点
 *
 * hooray for linked lists
 *
 * @param l
 * @param t
 */
void addtask(Tasklist *l, Task *t)
{
    if (l->tail) {
        /* 往尾部追加 */
        l->tail->next = t;
        t->prev = l->tail;
    } else {
        /* 没有尾部说明是空链表, 直接设置头 */
        l->head = t;
        t->prev = nil;
    }

    l->tail = t; /* 设置新尾巴 */
    t->next = nil;
}

/**
 * @brief 从双向链表里面删除 task
 *
 * @param l 链表对象
 * @param t 元素对象
 */
void deltask(Tasklist *l, Task *t)
{
    if (t->prev) {
        t->prev->next = t->next;
    } else {
        l->head = t->next; /* t 是链表头结点 */
    }

    if (t->next) {
        t->next->prev = t->prev;
    } else {
        l->tail = t->prev; /* t 是链表尾节点 */
    }
}

/**
 * @brief 返回当前正在运行的协程 id
 *
 * @return unsigned int
 */
unsigned int taskid(void)
{
    return taskrunning->id;
}
