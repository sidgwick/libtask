#include "taskimpl.h"
#include <fcntl.h>
#include <sys/poll.h>

enum { MAXFD = 1024 };

static struct pollfd pollfd[MAXFD];
static Task *polltask[MAXFD];
static int npollfd;
static int startedfdtask;
static Tasklist sleeping;
static int sleepingcounted;
static uvlong nsec(void);

/**
 * @brief 执行文件描述符相关的事件协程
 *
 * 实际上这个函数在没有任何 pollfd 的时候也可以工作:
 *  - 可以用来做协程 sleep 使用, 参考下面的 taskdelay 函数实现
 *
 * @param v 此参数无用
 */
void fdtask(void *v)
{
    int i, ms;
    Task *t;
    uvlong now;

    tasksystem();
    taskname("fdtask");
    for (;;) {
        /* let everyone else run */
        while (taskyield() > 0)
            ;

        /* 到此, 已经没有其他协程在等待调度了 */

        /* we're the only one runnable - poll for i/o */
        errno = 0;
        taskstate("poll");

        /* 如果没有睡眠等待队列, 直接 poll 阻塞等待文件描述符事件
         * 否则 poll 按照用户设计的 alarmtime 最多等 5s, 然后超时 */
        if ((t = sleeping.head) == nil) {
            ms = -1;
        } else {
            /* sleep at most 5s */
            now = nsec();
            if (now >= t->alarmtime) {
                ms = 0;
            } else if (now + 5 * 1000 * 1000 * 1000LL >= t->alarmtime) {
                ms = (t->alarmtime - now) / 1000000;
            } else {
                ms = 5000;
            }
        }

        /* poll 系统调用, 如果出错返回负数, 超时返回 0, 有事件发生返回事件数量 */
        if (poll(pollfd, npollfd, ms) < 0) {
            if (errno == EINTR) {
                /* 系统调用如果是被中断打断了
                 * TODO: 检查 Linux 的 signal 处理时刻, 重新执行系统调用的逻辑 */
                continue;
            }

            fprint(2, "poll: %s\n", strerror(errno));
            taskexitall(0);
        }

        /* wake up the guys who deserve it */
        for (i = 0; i < npollfd; i++) {

            /* 因为 while block 会把最后一个 pollfd, 移动到 i 位置,
             * 因此这里需要使用 while 确保新移动过来的 pollfd 也能得到处理 */
            while (i < npollfd && pollfd[i].revents) {
                taskready(polltask[i]);
                --npollfd;
                pollfd[i] = pollfd[npollfd];
                polltask[i] = polltask[npollfd];
            }
        }

        now = nsec();

        /* sleeping 里面是等待睡眠超时的任务
         * 如果当前时间已经达到超时时间, 就将任务移动到就绪队列 */
        while ((t = sleeping.head) && now >= t->alarmtime) {
            deltask(&sleeping, t);

            /* 参考 taskdelay 实现, 有睡眠任务的时候 taskcount 会冗余加 1,
             * 这里因为睡眠完成需要把那个冗余的计数减去 */
            if (!t->system && --sleepingcounted == 0) {
                taskcount--;
            }

            taskready(t);
        }
    }
}

/**
 * @brief 任务延时指定的毫秒数
 *
 * @param ms
 * @return uint
 */
uint taskdelay(uint ms)
{
    uvlong when, now;
    Task *t;

    /* fdtask 是具体的睡眠逻辑, 可以把它当成定时器的角色 */
    if (!startedfdtask) {
        startedfdtask = 1;
        taskcreate(fdtask, 0, 32768);
    }

    now = nsec();
    when = now + (uvlong)ms * 1000000;

    /* sleeping 里面的进程, 是按照睡眠时间从小到大排列的
     * 这里找到第一个睡眠时间比参数指定的 ms 大的任务节点 */
    for (t = sleeping.head; t != nil && t->alarmtime < when; t = t->next)
        ;

    /* 如果有时间比参数指定的时间大的节点存在, 将当前任务插到这个节点之前
     * 否则的话, 参数指定的时间是现在所有睡眠任务中时间最大的那个, 因此放到链表最后
     * 这一步先维护当前任务的节点指针, 插入动作在后面的 if/else 里面
     *
     * 注意住调度器运行 taskrunning 的时候, 已经将它从 taskrunqueue 链表中摘除,
     * 因此这里不需要摘除操作 */
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

    if (t->next) {
        t->next->prev = t;
    } else {
        sleeping.tail = t;
    }

    /* 如果 t 不是系统任务, sleepingcounted 计数加 1, 任务统计数量加 1
     * 这里维护 taskcount, 是因为 fdtask 被标记为 system 任务, 为了避免调度器在睡眠
     * 任务睡眠过程中退出, 这里需要登记睡眠任务也在任务数量计数范围之内
     *
     * TODO: 这个计数是冗余的, 正常情况下睡眠的任务计数已经在 taskcount 里面了, 这里属于额外再加一次 */
    if (!t->system && sleepingcounted++ == 0) {
        taskcount++;
    }

    taskswitch();

    return (nsec() - now) / 1000000;
}

/**
 * @brief 等待文件描述符出现读写事件
 *
 * @param fd
 * @param rw
 */
void fdwait(int fd, int rw)
{
    int bits;

    /* fdtask 是具体的等待逻辑 */
    if (!startedfdtask) {
        startedfdtask = 1;
        taskcreate(fdtask, 0, 32768);
    }

    if (npollfd >= MAXFD) {
        fprint(2, "too many poll file descriptors\n");
        abort();
    }

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

    polltask[npollfd] = taskrunning;
    pollfd[npollfd].fd = fd;
    pollfd[npollfd].events = bits;
    pollfd[npollfd].revents = 0;
    npollfd++;
    taskswitch();
}

/**
 * @brief 从文件描述符读取数据
 *
 * Like fdread but always calls fdwait before reading.
 * 无论文件是否就绪, 都先调用一次 fdwait
 *
 * @param fd 文件描述符
 * @param buf 读取数据缓冲区
 * @param n 要读取的字节数量
 * @return int 实际读取的字节数量
 */
int fdread1(int fd, void *buf, int n)
{
    int m;

    do {
        fdwait(fd, 'r');
    } while ((m = read(fd, buf, n)) < 0 && errno == EAGAIN);

    return m;
}

/**
 * @brief 从文件描述符读取数据
 *
 * 如果文件暂时不能读, 使用 fdwait 阻塞, 直到允许读取
 *
 * @param fd 文件描述符
 * @param buf 读取数据缓冲区
 * @param n 要读取的字节数量
 * @return int 实际读取的字节数量
 */
int fdread(int fd, void *buf, int n)
{
    int m;

    while ((m = read(fd, buf, n)) < 0 && errno == EAGAIN) {
        fdwait(fd, 'r');
    }

    return m;
}

/**
 * @brief 向文件描述符写入数据
 *
 * 如果文件暂时不能写, 使用 fdwait 阻塞, 直到允许写入
 *
 * @param fd 文件描述符
 * @param buf 数据缓冲区
 * @param n 要写入的字节数量
 * @return int 实际写入的字节数量
 */
int fdwrite(int fd, void *buf, int n)
{
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

/**
 * @brief 设置文件描述符 fd 为不阻塞模式
 *
 * @param fd
 * @return int
 */
int fdnoblock(int fd)
{
    return fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
}

/**
 * @brief 获取当前时间的纳秒表示
 *
 * @return uvlong
 */
static uvlong nsec(void)
{
    struct timeval tv;

    if (gettimeofday(&tv, 0) < 0) {
        return -1;
    }

    return (uvlong)tv.tv_sec * 1000 * 1000 * 1000 + tv.tv_usec * 1000;
}
