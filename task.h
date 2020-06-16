/* Copyright (c) 2005 Russ Cox, MIT; see COPYRIGHT */

#ifndef _TASK_H_
#define _TASK_H_ 1

#ifdef __cplusplus
extern "C" {
#endif

#include <inttypes.h>
#include <stdarg.h>

/*
 * basic procs and threads
 */

typedef struct Task Task;
typedef struct Tasklist Tasklist; /* Tasklist 是追踪所有协程对象的双向链表 */

int anyready(void);
int taskcreate(void (*f)(void *arg), void *arg, unsigned int stacksize);
void taskexit(int);
void taskexitall(int);
void taskmain(int argc, char *argv[]); /* 需要由用户提供的程序入口函数 */
int taskyield(void);
void **taskdata(void);
void needstack(int);
void taskname(char *, ...);
void taskstate(char *, ...);
char *taskgetname(void);
char *taskgetstate(void);
void tasksystem(void);
unsigned int taskdelay(unsigned int);
unsigned int taskid(void);

struct Tasklist /* used internally */
{
    Task *head;
    Task *tail;
};

/*
 * queuing locks
 *
 * 锁实现
 */
typedef struct QLock QLock;
struct QLock {
    Task *owner;
    Tasklist waiting;
};

void qlock(QLock *);
int canqlock(QLock *);
void qunlock(QLock *);

/*
 * reader-writer locks
 *
 * 读写锁
 */
typedef struct RWLock RWLock;
struct RWLock {
    int readers;       /* 持有读锁的数量 */
    Task *writer;      /* 写协程 */
    Tasklist rwaiting; /* 等待获取读锁的协程列表 */
    Tasklist wwaiting; /* 等待获取写锁的协程列表 */
};

void rlock(RWLock *);
int canrlock(RWLock *);
void runlock(RWLock *);

void wlock(RWLock *);
int canwlock(RWLock *);
void wunlock(RWLock *);

/*
 * sleep and wakeup (condition variables)
 */
typedef struct Rendez Rendez;

struct Rendez {
    QLock *l;
    Tasklist waiting;
};

void tasksleep(Rendez *);
int taskwakeup(Rendez *);
int taskwakeupall(Rendez *);

/*
 * channel communication
 */
typedef struct Alt Alt;
typedef struct Altarray Altarray;
typedef struct Channel Channel;

enum {
    CHANEND,   /* 标志当前位置已经处于通道的末尾位置 */
    CHANSND,   /* 发送数据操作 */
    CHANRCV,   /* 接受数据操作 */
    CHANNOP,   /* 无操作占位标记 */
    CHANNOBLK, /* 不阻塞标记 */
};

struct Alt {
    Channel *c;      /* alt 元素对应的 channel */
    void *v;         /* 值 */
    unsigned int op; /* 操作 */
    Task *task;      /* 对应的协程 */
    Alt *xalt;       /* Alt 元素对应的操作暂存数据, 这个指针主要用于将来清理这些暂存数据 */
};

struct Altarray {
    Alt **a;
    unsigned int n; /* a 字段中有效元素数量 */
    unsigned int m; /* a 字段可容纳元素数量 */
};

struct Channel {
    unsigned int elemsize; /* channel 中现有元素的大小 */

    unsigned char *buf;   /* 元素 buffer */
    unsigned int bufsize; /* buf 最多可以容纳元素数量 */
    unsigned int nbuf;    /* 现在 buf 里面有所少元素 */
    unsigned int off;     /* 当前读取/写入通道的 offset */
    Altarray asend;       /* 发送通道 */
    Altarray arecv;       /* 接受通道 */
    char *name;
};

int chanalt(Alt *alts);
Channel *chancreate(int elemsize, int elemcnt);
void chanfree(Channel *c);
int chaninit(Channel *c, int elemsize, int elemcnt);
int channbrecv(Channel *c, void *v);
void *channbrecvp(Channel *c);
unsigned long channbrecvul(Channel *c);
int channbsend(Channel *c, void *v);
int channbsendp(Channel *c, void *v);
int channbsendul(Channel *c, unsigned long v);
int chanrecv(Channel *c, void *v);
void *chanrecvp(Channel *c);
unsigned long chanrecvul(Channel *c);
int chansend(Channel *c, void *v);
int chansendp(Channel *c, void *v);
int chansendul(Channel *c, unsigned long v);

/*
 * Threaded I/O.
 */
int fdread(int, void *, int);
int fdread1(int, void *, int); /* always uses fdwait */
int fdwrite(int, void *, int);
void fdwait(int, int);
int fdnoblock(int);

void fdtask(void *);

/*
 * Network dialing - sets non-blocking automatically
 */
enum {
    UDP = 0,
    TCP = 1,
};

int netannounce(int, char *, int);
int netaccept(int, char *, int *);
int netdial(int, char *, int);
int netlookup(char *, uint32_t *); /* blocks entire program! */
int netdial(int, char *, int);

#ifdef __cplusplus
}
#endif
#endif
