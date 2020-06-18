#include <fcntl.h>
#include <sys/poll.h>

#include "taskimpl.h"

enum {
    MAXFD = 1024
};

static struct pollfd pollfd[MAXFD];
static Task *polltask[MAXFD];
static int npollfd;
static int startedfdtask;
static Tasklist sleeping;
static int sleepingcounted;
static uvlong nsec(void);

void fdtask(void *v) {
    int i, ms;
    Task *t;
    uvlong now;

    tasksystem();
    taskname("fdtask");
    for (;;) {
        /* let everyone else run
         * 这里让出 CPU, 如果走到了这个 while 后面的逻辑, 就意味着全部的协程都阻塞,
         * 所以这里在不断地让出资源, 效果就是如果有除了当前协程和调度器协程之外
         * 的其他协程运行, 优先运行这些协程.
         *
         * 这波操作意味着, 如果有另外一个协程不断地在 "execute -> yield -> execute -> yield",
         * 这里的 poll 动作将会永远都执行不到
         *
         * httpload.c yield_block_fdtask 函数可以验证上面判断 */
        while (taskyield() > 0)
            ;

        /* we're the only one runnable - poll for i/o */
        errno = 0;
        taskstate("poll");

        /* 根据 sleeping 的协程数量, 决定怎么做超时操作
         * 没有 sleeping 意味着没有协程为自己设置了延时执行, 这中只需要阻塞在 poll 就好了,
         * 有的话我们必须设置超时时间, 以便到点可以顺利的执行 delay task.
         *
         * redis ae.c 里面在处理时间事件的时候也有非常类似的逻辑
         * 这实际上就是一个简单的 timer: http://whosemario.github.io/2015/11/12/timer/ */
        if ((t = sleeping.head) == nil) {
            ms = -1;
        } else {
            /* sleep at most 5s */
            now = nsec();
            if (now >= t->alarmtime) {
                ms = 0;
            } else if (now + 5 * 1000 * 1000 * 1000LL >= t->alarmtime) {
                /* 如果 t 设定自己 alarmtime 被唤醒, 应该计算到这个时间点纳秒数(不过也不能超过 5s) */
                ms = (t->alarmtime - now) / 1000000;
            } else {
                ms = 5000;
            }
        }

        if (poll(pollfd, npollfd, ms) < 0) {
            if (errno == EINTR)
                continue;

            fprint(2, "poll: %s\n", strerror(errno));
            taskexitall(0);
        }

        /* wake up the guys who deserve it */
        for (i = 0; i < npollfd; i++) {
            while (i < npollfd && pollfd[i].revents) {
                taskready(polltask[i]); /* 唤醒协程, 使之可以被调度 */
                --npollfd;

                /* 数组缩容后将最后一个元素填充到删除位置 */
                pollfd[i] = pollfd[npollfd];
                polltask[i] = polltask[npollfd];
            }
        }

        /* 当前有正在睡眠等待任务
         * 遍历要求延时执行的任务, 符合执行时机条件的, 将它们从延迟队列里面移除并标注为 ready */
        now = nsec();
        while ((t = sleeping.head) && now >= t->alarmtime) {
            deltask(&sleeping, t);

            /* t 不是系统任务, 并且他是最后一个延时任务, 配合 taskdelay 的处理逻辑看 */
            if (!t->system && --sleepingcounted == 0)
                taskcount--;

            taskready(t);
        }
    }
}

/* 这个函数是在向 sleeping 队列中适当的位置添加元素
 * 所谓适当的位置就是协程的 alarmtime 在链表中由小到大顺序排列 */
uint taskdelay(uint ms) {
    uvlong when, now;
    Task *t;

    /* 单独起一个协程, 用来处理文件描述符读写事件 */
    if (!startedfdtask) {
        startedfdtask = 1;
        taskcreate(fdtask, 0, 32768);
    }

    /* 算出 ms 对应的过期时间, 单位纳秒 */
    now = nsec();
    when = now + (uvlong)ms * 1000000;
    for (t = sleeping.head; t != nil && t->alarmtime < when; t = t->next)
        ;

    /* 如果在睡眠列表里面找到数据, 将 taskrunning 节点插到 sleeping 队列的 t 位置 */
    if (t) {
        taskrunning->prev = t->prev;
        taskrunning->next = t;
    } else {
        taskrunning->prev = sleeping.tail;
        taskrunning->next = nil;
    }

    t = taskrunning;
    t->alarmtime = when;

    if (t->prev) {
        t->prev->next = t;
    } else {
        sleeping.head = t;
    }

    if (t->next)
        t->next->prev = t;
    else
        sleeping.tail = t;

    /* 这里, 如果 t 不是系统任务, 并且它又是第一个延时任务, 那么给 taskcount 计数 +1
     * 这样做原因是, 防止有延时任务还没执行完程序就被强制退出了(TODO: 什么情况会出现这个?)
     *
     * 可以配合 taskexit 函数理解这里 */
    if (!t->system && sleepingcounted++ == 0)
        taskcount++;

    taskswitch();

    return (nsec() - now) / 1000000;
}

/* 等待 fd 上面发生读/写事件 */
void fdwait(int fd, int rw) {
    int bits;

    if (!startedfdtask) {
        startedfdtask = 1;
        taskcreate(fdtask, 0, 32768);
    }

    if (npollfd >= MAXFD) {
        fprint(2, "too many poll file descriptors\n");
        abort();
    }

    /* 当前协程(调用 fdwait 那个)任务状态, 在等待什么事件的发生 */
    taskstate("fdwait for %s", rw == 'r' ? "read" : rw == 'w' ? "write" : "error");

    bits = 0;
    switch (rw) {
        case 'r':
            bits |= POLLIN;
            break;
        case 'w':
            bits |= POLLOUT;
            break;
    }

    polltask[npollfd] = taskrunning; /* 记录 fd 对应的任务 */
    pollfd[npollfd].fd = fd;         /* 组合 poll 使用的数据结构 */
    pollfd[npollfd].events = bits;
    pollfd[npollfd].revents = 0;
    npollfd++;
    taskswitch(); /* 当前协程切出去, 让出 CPU 资源 */
}

/* Like fdread but always calls fdwait before reading.
 * 这个函数不管 fd 是否读取就绪, 直接 wait 一下然后再读 */
int fdread1(int fd, void *buf, int n) {
    int m;

    do {
        fdwait(fd, 'r'); /* 这里就实现了阻塞读取的效果 */
    } while ((m = read(fd, buf, n)) < 0 && errno == EAGAIN);

    return m;
}

/* 这个先尝试读取, 没数据才开始 wait */
int fdread(int fd, void *buf, int n) {
    int m;

    while ((m = read(fd, buf, n)) < 0 && errno == EAGAIN) {
        fdwait(fd, 'r');
    }

    return m;
}

/* 写数据包装 */
int fdwrite(int fd, void *buf, int n) {
    int m, tot;

    for (tot = 0; tot < n; tot += m) {
        while ((m = write(fd, (char *)buf + tot, n - tot)) < 0 && errno == EAGAIN) {
            fdwait(fd, 'w');
        }

        if (m < 0) {
            return m;
        }

        if (m == 0) {
            break;
        }
    }

    return tot;
}

/* 设置 fd 为非阻塞 */
int fdnoblock(int fd) {
    return fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
}

/* 计算当前的纳秒数 */
static uvlong nsec(void) {
    struct timeval tv;

    if (gettimeofday(&tv, 0) < 0)
        return -1;

    return (uvlong)tv.tv_sec * 1000 * 1000 * 1000 + tv.tv_usec * 1000;
}
