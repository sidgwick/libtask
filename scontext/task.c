#include "task.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct coroutine coroutine_t;
typedef struct scheduler scheduler_t;
typedef int(entrance)(coroutine_t *, void *); /* 返回值表示是否需要继续被调度 */

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

void make_context(scontext_t *ctx, entrance *f) {
    int *sp = (int *)ctx->stack.ss_sp + ctx->stack.ss_size / 4;

    ctx->mcontext.eip = (uint64_t)f;
    ctx->mcontext.esp = (uint64_t)sp;
}

void swapcontext(scheduler_t *s, coroutine_t *c) {
    /* 先保存, 再交换 */
    char dummy = 0;

    getcontext(&s->ctx.mcontext); /* 保存上下文 */
    s->ctx.stack.ss_sp = &dummy;
    // s->ctx.stack.ss_size = size;

    setcontext(&c->ctx.mcontext);
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
    make_context(&c->ctx, f);

    return c;
}

coroutine_t *create_coroutine(scheduler_t *s, entrance *f, void *arg, size_t size) {
    if (s->size + 1 >= s->cap) {
        fprintf(stderr, "too many coroutine created!\n");
        exit(1);
    }

    coroutine_t *c = _create_coroutine(f, arg, size);

    s->coroutines[s->size++] = c;

    return c;
}

void coroutine_yield(coroutine_t *c) {
    c->status = WAITING;

    char dummy = 0;

    getcontext(&c->ctx.mcontext); /* 保存上下文 */
    c->ctx.stack.ss_sp = &dummy;
    c->ctx.stack.ss_size = c->stack_size;
}

int c_main(coroutine_t *c, void *arg) {
    for (int i = 0; i < 5; i++) {
        printf("coroutine: %d(%s), circle: %d", c->id, (char *)arg, i);
        coroutine_yield(c);
    }

    return 0;
}

void scheduler_run(scheduler_t *s) {
    while (s->size > 0) {
        for (int i = 0; i < s->size; i++) {
            coroutine_t *c = s->coroutines[i];

            if (c->status == READY) {
                // 执行这个协程
                s->current = c->id;
                swapcontext(s, c);
                s->current = 0;
            } else if (c->status == STOPPED) {
                // 删掉这个协程
                s->coroutines[i] = s->coroutines[s->size--];
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
        coroutine_t *t = create_coroutine(s, c_main, (void *)x, 256);
        printf("create coroutine %d done.\n", t->id);
    }

    scheduler_run(s);
}