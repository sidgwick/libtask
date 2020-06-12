/* Copyright (c) 2005 Russ Cox, MIT; see COPYRIGHT */

#include <fcntl.h>
#include <stdio.h>

#include "taskimpl.h"

int taskdebuglevel;
int taskcount;
int tasknswitch;
int taskexitval;   /* 当前正在运行中协程退出码 */
Task *taskrunning; /* 指向当前正在运行的协程对象 */

Context taskschedcontext;
Tasklist taskrunqueue;

Task **alltask;
int nalltask;

static char *argv0;
static void contextswitch(Context *from, Context *to);

static void taskdebug(char *fmt, ...) {
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

/* 执行函数, y, x 是分裂了的指针. 执行完成之后就标记退出 */
static void taskstart(uint y, uint x) {
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

static int taskidgen;

/* 创建一个协程对象, 需要指明
 * 1. 入口函数
 * 2. 入口函数参数
 * 3. 运行时栈大小 */
static Task *taskalloc(void (*fn)(void *), void *arg, uint stack) {
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
    t->stk = (uchar *)(t + 1); /* Task* 类型的指针 +1 之后指向 (char *)t + sizeof(Task) 后面的部分 */
    t->stksize = stack;        /* 运行时栈大小 */
    t->id = ++taskidgen;       /* 协程 id */

    /* 入口函数与函数参数 */
    t->startfn = fn;
    t->startarg = arg;

    /* do a reasonable initialization */
    memset(&t->context.uc, 0, sizeof t->context.uc);
    sigemptyset(&zero);
    sigprocmask(SIG_BLOCK, &zero, &t->context.uc.uc_sigmask); /* TODO: 这里的含义是什么? */

    /* must initialize with current context */
    if (getcontext(&t->context.uc) < 0) {
        fprint(2, "getcontext: %r\n");
        abort();
    }

    /* call makecontext to do the real work. */
    /* leave a few words open on both ends */
    /* 栈空间在开始结束都留有一部分, 可能是怕栈溢出带来危害 */
    t->context.uc.uc_stack.ss_sp = t->stk + 8;
    t->context.uc.uc_stack.ss_size = t->stksize - 64;
#if defined(__sun__) && !defined(__MAKECONTEXT_V2_SOURCE) /* sigh */
#warning "doing sun thing"
    /* can avoid this with __MAKECONTEXT_V2_SOURCE but only on SunOS 5.9 */
    t->context.uc.uc_stack.ss_sp =
        (char *)t->context.uc.uc_stack.ss_sp + t->context.uc.uc_stack.ss_size;
#endif
    /*
	 * All this magic is because you have to pass makecontext a
	 * function that takes some number of word-sized variables,
	 * and on 64-bit machines pointers are bigger than words.
	 */
    //print("make %p\n", t);
    z = (ulong)t;
    y = z;
    z >>= 16; /* hide undefined 32-bit shift from 32-bit compilers */
    x = z >> 16;
    makecontext(&t->context.uc, (void (*)())taskstart, 2, y, x);

    return t;
}

/* 创建携程对象, 并将它添加到 alltask 管理池里面 */
int taskcreate(void (*fn)(void *), void *arg, uint stack) {
    int id;
    Task *t;

    t = taskalloc(fn, arg, stack);
    taskcount++;
    id = t->id;
    if (nalltask % 64 == 0) {
        alltask = realloc(alltask, (nalltask + 64) * sizeof(alltask[0]));
        if (alltask == nil) {
            fprint(2, "out of memory\n");
            abort();
        }
    }

    t->alltaskslot = nalltask;
    alltask[nalltask++] = t;
    taskready(t); /* 标记协程当前已经 ready */
    return id;
}

/* TODO: 这是啥? */
void tasksystem(void) {
    if (!taskrunning->system) {
        taskrunning->system = 1;
        --taskcount;
    }
}

/* 协程切换 */
void taskswitch(void) {
    needstack(0);

    /* 使用 POSIX 标准提供的上下文切换功能切换上下文并运行 taskschedcontext 上下文中保存的协程
     * TODO: 弄懂切换过程的细节(保存寄存器数据/设置新的执行栈?) */
    contextswitch(&taskrunning->context, &taskschedcontext);
}

/* 标记协程已经 ready -- 意味着协程下一步就可以被执行了 */
void taskready(Task *t) {
    t->ready = 1;
    addtask(&taskrunqueue, t);
}

/* 挂起一个协程, 让出资源 */
int taskyield(void) {
    int n;

    n = tasknswitch;
    taskready(taskrunning); /* 先标记状态, 并加入到链表队列中 */
    taskstate("yield");
    taskswitch();

    return tasknswitch - n - 1;
}

/* 判断是否有就绪的协程存在 */
int anyready(void) {
    return taskrunqueue.head != nil;
}

/* 以 val 作为错误码退出程序主体 */
void taskexitall(int val) {
    exit(val);
}

/* 标记协程退出 */
void taskexit(int val) {
    taskexitval = val;
    taskrunning->exiting = 1;
    taskswitch();
}

/* 切换执行协程 */
static void contextswitch(Context *from, Context *to) {
    if (swapcontext(&from->uc, &to->uc) < 0) {
        fprint(2, "swapcontext failed: %r\n");
        assert(0);
    }
}

/* 协程调度器 */
static void taskscheduler(void) {
    int i;
    Task *t;

    taskdebug("scheduler enter");
    for (;;) {
        /* 在死循环里面调度各个协程 */
        if (taskcount == 0)
            exit(taskexitval);

        t = taskrunqueue.head;
        if (t == nil) {
            fprint(2, "no runnable tasks! %d tasks stalled\n", taskcount);
            exit(1);
        }

        /* 先从待运行池里面捞出来 */
        deltask(&taskrunqueue, t);

        /* 标记 ready 可执行状态为 0 */
        t->ready = 0;
        taskrunning = t; /* 赋值给当前执行协程 */
        tasknswitch++;   /* 协程切换计数器 +1 */

        taskdebug("run %d (%s)", t->id, t->name);
        /* 从主协程切换到目标协程
         * 目标协程在执行过程中, 这里是不能继续执行的, 需要等目标协程交回控制权 */
        contextswitch(&taskschedcontext, &t->context);

        //print("back in scheduler\n");
        taskrunning = nil;

        /* 如果目标协程已经终止, 清理相应的资源 */
        if (t->exiting) {
            if (!t->system)
                taskcount--;

            i = t->alltaskslot;
            alltask[i] = alltask[--nalltask]; /* 把最后那个协程对象移动到删掉的对象占据的槽位上 */
            alltask[i]->alltaskslot = i;
            free(t); /* 释放对象 */
        }
    }
}

/* 返回协程附带的用户数据 */
void **taskdata(void) {
    return &taskrunning->udata;
}

/* debugging, 组装出来协程名字然后设定到当前运行的协程上来 */
void taskname(char *fmt, ...) {
    va_list arg;
    Task *t;

    t = taskrunning;
    va_start(arg, fmt);
    vsnprint(t->name, sizeof t->name, fmt, arg);
    va_end(arg);
}

/* 返回协程名字字段 */
char *taskgetname(void) {
    return taskrunning->name;
}

/* 协程的状态 */
void taskstate(char *fmt, ...) {
    va_list arg;
    Task *t;

    t = taskrunning;
    va_start(arg, fmt);
    vsnprint(t->state, sizeof t->name, fmt, arg);
    va_end(arg);
}

char *taskgetstate(void) {
    return taskrunning->state;
}

void needstack(int n) {
    Task *t;

    t = taskrunning;

    /* 前置知识: 栈是从高往低生长
     *
     * 这里申明 t 变量, 从栈上为它分配存储空间, 因此 t 位于当前执行的协程(也即: taskrunning)的栈顶
     * 
     * (char *)&t 这个表达式算出来的就是 t 变量所在的内存地址, 也即当前执行协程的栈顶
     * (char *)t->stk 指向当前执行协程的栈底
     * 
     * 下面的代码就很明了了 */

    if ((char *)&t <= (char *)t->stk || (char *)&t - (char *)t->stk < 256 + n) {
        fprint(2, "task stack overflow: &t=%p tstk=%p n=%d\n", &t, t->stk, 256 + n);
        abort();
    }
}

/* 输出协程信息 */
static void taskinfo(int s) {
    int i;
    Task *t;
    char *extra;

    fprint(2, "task list:\n");
    for (i = 0; i < nalltask; i++) {
        t = alltask[i];
        if (t == taskrunning)
            extra = " (running)";
        else if (t->ready)
            extra = " (ready)";
        else
            extra = "";
        fprint(2, "%6d%c %-20s %s%s\n", t->id, t->system ? 's' : ' ', t->name, t->state, extra);
    }
}

/*
 * startup
 */
static int taskargc;
static char **taskargv;
int mainstacksize;

static void taskmainstart(void *v) {
    taskname("taskmain"); /* 设定当前协程名字叫做 taskmain */

    taskmain(taskargc, taskargv); /* taskmain 函数由使用 tasklib 的用户实现, 这就是入口函数了 */
}

int main(int argc, char **argv) {
    struct sigaction sa, osa;

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = taskinfo; /* 利用信号来触发 taskinfo 函数, 输出协程的相关信息 */
    sa.sa_flags = SA_RESTART;
    sigaction(SIGQUIT, &sa, &osa);

#ifdef SIGINFO
    sigaction(SIGINFO, &sa, &osa);
#endif

    argv0 = argv[0]; /* 程序名字, libtask 是一个库, 这个程序名字是编译成二进制之后用户写的那个程序名字 */

    taskargc = argc;
    taskargv = argv;

    if (mainstacksize == 0)
        mainstacksize = 256 * 1024; /* 需要的栈大小 */

    taskcreate(taskmainstart, nil, mainstacksize); /* 创建主协程对象 */
    taskscheduler();                               /* 开始调度协程执行 */
    fprint(2, "taskscheduler returned in main!\n");
    abort();
    return 0;
}

/*
 * hooray for linked lists
 */
void addtask(Tasklist *l, Task *t) {
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

/* 从双向链表里面删除 task */
void deltask(Tasklist *l, Task *t) {
    if (t->prev)
        t->prev->next = t->next;
    else
        l->head = t->next;

    if (t->next)
        t->next->prev = t->prev;
    else
        l->tail = t->prev;
}

/* 返回当前正在运行的协程 id */
unsigned int taskid(void) {
    return taskrunning->id;
}
