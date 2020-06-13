#include "task.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct coroutine coroutine_t;
typedef struct scheduler scheduler_t;

typedef void(entrance)(coroutine_t *, void *); /* 用户实现的 coroutine 入口函数, 这里支持用户操作协程对象 */

int generate_id = 1;
enum {
    READY,
    RUNNING,
    WAITING,
    STOPPED,
};

struct coroutine {
    int id;     /* 协程 ID */
    int status; /* 携程运行状态 */

    entrance *main;
    void *arg;

    scheduler_t *s; /* 自己归属的调度器 */
    scontext_t ctx; /* 协程上下文 */

    int stack_size; /* 协程栈大小 */
    char stack[0];  /* 协程栈 */
};

struct scheduler {
    int current; /* 当前运行的协程 ID */

    scontext_t ctx; /* 调度器上下文 */

    int cap;                    /* coroutines 大小 */
    int size;                   /* 当前维护的 coroutines 数量 */
    coroutine_t *coroutines[0]; /* 维护的协程列表 */
};

/* 切换上下文 */
int swapcontext(smcontext_t *from, const smcontext_t *to) {
    /* gr == 0 是正常流程中调用的返回值
     * gr == 1 是 setcontext 函数切换上下文回来到这个位置的效果
     *
     * 很明显这里我们不希望 setcontext 切换回来之后还去执行 setcontext.
     * 因为如果 to 上下文是之前某次调用本函数保存的上下文状态, 那么这样做会
     * 让程序不断地在 getcontext/setcontext 之间跳转, 会产生类似死循环的效果
     */
    int gr = getcontext(from);
    if (gr == 0) {
        setcontext(to);
    }

    return 0;
}

/* 协程的入口函数 */
void coroutine_entry(coroutine_t *c) {
    c->main(c, c->arg);
    c->status = STOPPED;

    /* 执行完成之后, 切回主协程继续执行 */
    swapcontext(&c->ctx.mcontext, &c->s->ctx.mcontext);
}

/* 创建上下文
 * 主要做两件事:
 *
 * 1. 正确设置 %eip, 让协程切换后能进入入口函数执行
 * 2. 设置 coroutine_entry 函数的栈帧栈顶 %esp
 * 3. 无需花力气设置 %ebp, 我们无法从 coroutine_entry 函数里面退出来, 要依靠上下文切换切走
 *  */
void make_context(scontext_t *ctx, void *coroutine) {
    int *sp = (int *)ctx->stack.ss_sp + ctx->stack.ss_size / 4;

    /* 在栈空间上留下参数信息, 当进入 f 执行的时候就可以用了
     * 请注意栈是从大往小生长的 */
    *--sp = (int)coroutine;

    /* 这个是留给 coroutine_entry 函数返回之后执行的指令地址, 正常函数调用返回情况下,
     * 这个值会赋给 %eip, 但是 coroutine_entry 是通过切换上下文才能进入的, 它也不是靠 ret 指令退出函数,
     * 因此这个返回退出继续执行指令地址可以任意写一个值 */
    *--sp = 0;

    ctx->mcontext.eip = (long)coroutine_entry;
    ctx->mcontext.esp = (int)sp;
}

/* 创建一个协程调度器, size 是允许它调度的协程数量 */
scheduler_t *create_scheduler(size_t size) {
    scheduler_t *s = malloc(sizeof(scheduler_t) + sizeof(coroutine_t *) * size);

    s->current = 0;
    s->cap = size;
    s->size = 0;

    return s;
}

/* 创建协程对象 */
coroutine_t *_create_coroutine(entrance *f, void *arg, size_t size) {
    coroutine_t *c = malloc(sizeof(coroutine_t) + size);

    c->id = generate_id++;
    c->status = READY;

    c->main = f;
    c->arg = arg;

    c->stack_size = size;
    c->ctx.stack.ss_size = size;
    c->ctx.stack.ss_sp = c->stack;

    getcontext(&c->ctx.mcontext);
    make_context(&c->ctx, c);

    return c;
}

/* 创建协程, 硬追加到调度器里面 */
coroutine_t *create_coroutine(scheduler_t *s, entrance *f, void *arg, size_t size) {
    if (s->size + 1 >= s->cap) {
        fprintf(stderr, "too many coroutine created!\n");
        exit(1);
    }

    coroutine_t *c = _create_coroutine(f, arg, size);

    c->s = s;
    s->coroutines[s->size++] = c;

    return c;
}

/* 协程挂起 */
void coroutine_yield(coroutine_t *c) {
    c->status = WAITING;
    swapcontext(&c->ctx.mcontext, &c->s->ctx.mcontext);
}

/* 调度器主循环, 这个实现很简陋, 演示使用, 能说明问题就好了 */
void scheduler_run(scheduler_t *s) {
    while (s->size > 0) {
        for (int i = 0; i < s->size; i++) {
            coroutine_t *c = s->coroutines[i];

            if (c->status == READY) {
                // 执行这个协程
                s->current = c->id;
                //                printf("---- start to run coroutine %d ----\n", c->id);
                swapcontext(&s->ctx.mcontext, &c->ctx.mcontext);
                //                printf("---- end to run coroutine %d ----\n", c->id);
                s->current = 0;
            } else if (c->status == STOPPED) {
                // 删掉这个协程
                //                printf("---- coroutine %d deleted ----\n", c->id);
                s->coroutines[i] = s->coroutines[s->size - 1];

                s->size -= 1;
                i -= 1;
            } else if (c->status == WAITING) {
                c->status = READY;
            }
        }
    }
}

/************          EXAMPLE          *******************/
char *coroutine_name(int i) {
    char *s = malloc(8);
    sprintf(s, "RC-%d", i);

    return s;
}

/* 协程入口函数 */
void c_main(coroutine_t *c, void *arg) {
    printf("c_main run\n");
    for (int i = 0; i < 5; i++) {
        printf("%d(%s) loop round: %d\n", c->id, (char *)arg, i);
        coroutine_yield(c);
    }
}

int main() {
    /* 创建一个调度器, 调度器最多可以调度 128 个协程 */
    scheduler_t *s = create_scheduler(128);

    for (int i = 1; i < 4; i++) {
        /* 1MB 的栈大小足够了, 弄大点栈太小爆栈了会 Segmentation fault */
        char *x = coroutine_name(i);
        coroutine_t *t = create_coroutine(s, c_main, (void *)x, 1024 * 1024);
        printf("create coroutine %d done.\n", t->id);
    }

    scheduler_run(s);
    printf("all done.\n");
    return 0;
}
