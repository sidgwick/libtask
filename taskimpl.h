/* Copyright (c) 2005-2006 Russ Cox, MIT; see COPYRIGHT */

/* 文件做了精简, 只研究 i386 平台上的实现 */

/* 研究 libtask 自带的上下文切换实现, 不使用 OS 提供的相关功能 */
#define USE_UCONTEXT 0

#include "task.h"

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <sched.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define nil ((void *)0)
#define nelem(x) (sizeof(x) / sizeof((x)[0]))

#define ulong task_ulong
#define uint task_uint
#define uchar task_uchar
#define ushort task_ushort
#define uvlong task_uvlong
#define vlong task_vlong

typedef unsigned long ulong;
typedef unsigned int uint;
typedef unsigned char uchar;
typedef unsigned short ushort;
typedef unsigned long long uvlong;
typedef long long vlong;

int print(char *, ...);
int fprint(int, char *, ...);
char *snprint(char *, uint, char *, ...);
char *seprint(char *, char *, char *, ...);
int vprint(char *, va_list);
int vfprint(int, char *, va_list);
char *vsnprint(char *, uint, char *, va_list);
char *vseprint(char *, char *, char *, va_list);
char *strecpy(char *, char *, char *);

#include "386-ucontext.h"

typedef struct Context Context;

enum { STACK = 8192 };

struct Context {
    xucontext_t uc;
};

struct Task {
    char name[256];  /* 协程名称 */
    char state[256]; /* 协程状态描述 */
    Task *next;
    Task *prev;
    Task *allnext;
    Task *allprev;
    Context context;
    uvlong alarmtime; /* 协程的超时时间(fd.c) */

    uint id;      /* 协程 id */
    uchar *stk;   /* 栈底 */
    uint stksize; /* 栈大小 */
    int exiting;
    int alltaskslot; /* 在任务表中的编号 */
    int system;
    int ready;

    void (*startfn)(void *); /* 用户指定的协程入口函数 */
    void *startarg;          /* 用户指定的协程入口参数 */
    void *udata;
};

void taskready(Task *);
void taskswitch(void);

void addtask(Tasklist *, Task *);
void deltask(Tasklist *, Task *);

extern Task *taskrunning;
extern int taskcount;
