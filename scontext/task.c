#include "task.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct coroutine coroutine_t;
typedef struct scheduler scheduler_t;

typedef void(entrance)(coroutine_t *, void *); /* 返回值表示是否需要继续被调度 */

void coroutine_entry(coroutine_t *c);

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

void make_context(scontext_t *ctx, void *coroutine) {
    int *sp = (int *)ctx->stack.ss_sp + ctx->stack.ss_size / 4;

    /* 在栈空间上留下参数信息, 当进入 f 执行的时候就可以用了
     * 请注意栈是从大往小生长的 */
    // sp = (void *)((uintptr_t)sp - (uintptr_t)sp % 16); /* 16-align for OS X */
    // *--sp = (int)arg;
    *--sp = (int)coroutine;

    *--sp = 0; /* return address */

    ctx->mcontext.eip = (long)coroutine_entry;
    ctx->mcontext.esp = (int)sp;
}

int swapcontext(smcontext_t *from, const smcontext_t *to) {
    if (getcontext(from) == 0) {
        setcontext(to);
    }

    return 0;
}

/* 创建一个协程调度器, size 是允许它调度的协程数量 */
scheduler_t *create_scheduler(size_t size) {
    scheduler_t *s = malloc(sizeof(scheduler_t) + sizeof(coroutine_t *) * size);

    s->current = 0;
    s->cap = size;
    s->size = 0;

    return s;
}

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

void coroutine_yield(coroutine_t *c) {
    c->status = WAITING;
    swapcontext(&c->ctx.mcontext, &c->s->ctx.mcontext);
}

/* 协程的入口函数 */
void coroutine_entry(coroutine_t *c) {
    c->main(c, c->arg);
    c->status = STOPPED;

    /* 执行完成之后, 切回主协程继续执行 */
    swapcontext(&c->ctx.mcontext, &c->s->ctx.mcontext);
}

void c_main(coroutine_t *c, void *arg) {
    printf("c_main run\n");
    for (int i = 0; i < 5; i++) {
        printf("%d(%s) circle: %d\n", c->id, (char *)arg, i);
        coroutine_yield(c);
    }
}

void scheduler_run(scheduler_t *s) {
    while (s->size > 0) {
        for (int i = 0; i < s->size; i++) {
            coroutine_t *c = s->coroutines[i];

            if (c->status == READY) {
                // 执行这个协程
                s->current = c->id;
                printf("---- start to run coroutine %d ----\n", c->id);
                swapcontext(&s->ctx.mcontext, &c->ctx.mcontext);
                printf("---- end to run coroutine %d ----\n", c->id);
                s->current = 0;
            } else if (c->status == STOPPED) {
                // 删掉这个协程
                printf("---- coroutine %d deleted ----\n", c->id);
                s->coroutines[i] = s->coroutines[s->size-1];

                s->size -= 1;
                i -= 1;
            } else if (c->status == WAITING) {
                c->status = READY;
            }
        }
    }
}

char *coroutine_name(int i) {
    char *s = malloc(8);
    sprintf(s, "RC-%d", i);

    return s;
}

int main() {
    /* 创建一个调度器, 调度器最多可以调度 128 个协程 */
    scheduler_t *s = create_scheduler(128);

    for (int i = 1; i < 4; i++) {
        /* 256 的栈大小足够了 */
        char *x = coroutine_name(i);
        coroutine_t *t = create_coroutine(s, c_main, (void *)x, 12256);
        printf("create coroutine %d done.\n", t->id);
    }

    scheduler_run(s);
    return 0;
}