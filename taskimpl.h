/* Copyright (c) 2005-2006 Russ Cox, MIT; see COPYRIGHT */
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

#include "386-ucontext.h"
#include "task.h"

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

#define print task_print
#define fprint task_fprint
#define snprint task_snprint
#define seprint task_seprint
#define vprint task_vprint
#define vfprint task_vfprint
#define vsnprint task_vsnprint
#define vseprint task_vseprint
#define strecpy task_strecpy

int print(char *, ...);
int fprint(int, char *, ...);
char *snprint(char *, uint, char *, ...);
char *seprint(char *, char *, char *, ...);
int vprint(char *, va_list);
int vfprint(int, char *, va_list);
char *vsnprint(char *, uint, char *, va_list);
char *vseprint(char *, char *, char *, va_list);
char *strecpy(char *, char *, char *);

typedef struct Context Context;

enum {
    STACK = 8192
};

struct Context {
    sucontext_t uc;
};

/* task 定义了协程对象 */
struct Task {
    char name[256];  // offset known to acid
    char state[256];

    Task *next;
    Task *prev;

    Task *allnext;
    Task *allprev;

    Context context; /* 上下文 */
    uvlong alarmtime;

    uint id;      /* 协程 ID */
    uchar *stk;   /* 运行时栈 */
    uint stksize; /* 运行时栈大小 */

    int alltaskslot; /* 携程在 alltask 池里面的索引 */
    int system;

    int exiting;
    int ready;

    void (*startfn)(void *); /* 入口回调函数 */
    void *startarg;
    void *udata;
};

void taskready(Task *);
void taskswitch(void);

void addtask(Tasklist *, Task *);
void deltask(Tasklist *, Task *);

extern Task *taskrunning;
extern int taskcount;
