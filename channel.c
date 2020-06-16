/* Copyright (c) 2005 Russ Cox, MIT; see COPYRIGHT */

#include "taskimpl.h"

Channel *chancreate(int elemsize, int bufsize) {
    Channel *c;

    c = malloc(sizeof *c + bufsize * elemsize);
    if (c == nil) {
        fprint(2, "chancreate malloc: %r");
        exit(1);
    }
    memset(c, 0, sizeof *c);
    c->elemsize = elemsize; /* 单个元素大小 */
    c->bufsize = bufsize;   /* 元素总数量 */
    c->nbuf = 0;
    c->buf = (uchar *)(c + 1); /* 考虑使用柔性数组吧 */
    return c;
}

/* bug - work out races */
void chanfree(Channel *c) {
    if (c == nil)
        return;

    free(c->name);
    free(c->arecv.a);
    free(c->asend.a);
    free(c);
}

static void addarray(Altarray *a, Alt *alt) {
    if (a->n == a->m) {
        a->m += 16;
        a->a = realloc(a->a, a->m * sizeof a->a[0]);
    }

    a->a[a->n++] = alt;
}

static void delarray(Altarray *a, int i) {
    --a->n;
    a->a[i] = a->a[a->n];
}

/*
 * doesn't really work for things other than CHANSND and CHANRCV
 * but is only used as arg to chanarray, which can handle it
 */
#define otherop(op) (CHANSND + CHANRCV - (op))

/* 找到 channel 里面 op 操作对应的 Altarray */
static Altarray *chanarray(Channel *c, uint op) {
    switch (op) {
        default:
            return nil;
        case CHANSND:
            return &c->asend;
        case CHANRCV:
            return &c->arecv;
    }
}

/* 判断元素 a 能否在通道 a->c 上面执行 a->op 动作
 * a->op 动作就是 CHANSND 和 CHANRCV 操作
 *
 * 判断能否操作的标准是
 *
 * 1. 看是否有组否的缓冲区
 * 2. 查看相反的操作队列里面是否有足够的数据, 也即 CHANSND 操作查看 arecv 数组, CHANRCV 操作看 asend 数组
 *    这么做的原因是对无缓冲区通道而言(阻塞/非阻塞逻辑在 altcanexec 之外处理):
 *      1. 没有 arecv 说明没人读, 因此自己无法写
 *      2. 没有 asend 说明没人写, 因此自己无法读取
 */
static int altcanexec(Alt *a) {
    Altarray *ar;
    Channel *c;

    if (a->op == CHANNOP)
        return 0;
    c = a->c;
    if (c->bufsize == 0) {
        ar = chanarray(c, otherop(a->op));
        return ar && ar->n;
    } else {
        switch (a->op) {
            default:
                return 0;
            case CHANSND:
                return c->nbuf < c->bufsize;
            case CHANRCV:
                return c->nbuf > 0;
        }
    }
}

/* 将 a 加入到 channel 对应的操作队列中去 */
static void altqueue(Alt *a) {
    Altarray *ar;

    ar = chanarray(a->c, a->op);
    addarray(ar, a);
}

/* 将 a 从 channel 对应的操作队列中删去 */
static void altdequeue(Alt *a) {
    int i;
    Altarray *ar;

    ar = chanarray(a->c, a->op);
    if (ar == nil) {
        fprint(2, "bad use of altdequeue op=%d\n", a->op);
        abort();
    }

    for (i = 0; i < ar->n; i++)
        if (ar->a[i] == a) {
            delarray(ar, i);
            return;
        }
    fprint(2, "cannot find self in altdq\n");
    abort();
}

/* 找到 a 对应的数组中的元素, 并在它们所属的 channel 对应队列里面删除掉 */
static void altalldequeue(Alt *a) {
    int i;

    /* CHANEND/CHANNOBLK 都是一组 _chanop 函数参数的结束标志. 理解这句话请结合 altexec 函数的清理部分一起看 */
    for (i = 0; a[i].op != CHANEND && a[i].op != CHANNOBLK; i++)
        if (a[i].op != CHANNOP)
            altdequeue(&a[i]);
}

/* 内存拷贝辅助函数 */
static void amove(void *dst, void *src, uint n) {
    if (dst) {
        if (src == nil)
            memset(dst, 0, n);
        else
            memmove(dst, src, n);
    }
}

/*
 * Actually move the data around.  There are up to three
 * players: the sender, the receiver, and the channel itself.
 * If the channel is unbuffered or the buffer is empty,
 * data goes from sender to receiver.  If the channel is full,
 * the receiver removes some from the channel and the sender
 * gets to put some in.
 *
 * 这个函数用于将数据写入到 channel 或者从 channel 里面读取出来: 将数据从 s->v 拷贝到 r->v
 * 无论是读取还是写入操作, s 都是由 _chanop 函数提供的.
 *
 * 因此对应读(CHANRCV)操作的情况, 需要使用 s->v 将结果带出, 函数中对 s/r 的位置做了对调操作
 *
 * 读操作: altcopy(r, s) --> CHANECV
 * 写操作: altcopy(s, r) --> CHANSND
 *
 * 请注意, 程序中在调用这个函数之前, 是做了 altcanexec 判断的, 因此本函数里面的逻辑按直觉读就可以.
 * 函数中没有覆盖到的情况, altcanexec 不会允许这种情况发生
 */
static void altcopy(Alt *s, Alt *r) {
    Alt *t;
    Channel *c;
    uchar *cp;

    /*
	 * Work out who is sender and who is receiver
	 */
    if (s == nil && r == nil)
        return;

    /* 这个位置上 s 不可能为空. */
    assert(s != nil);

    c = s->c;
    if (s->op == CHANRCV) {
        t = s;
        s = r;
        r = t;
    }

    /* 上面的调换操作之后, 能保证 r 永远是接受数据(被写入的一方), s 永远是读取数据那一方
     * 交换操作会让下面的断言成立 */
    assert(s == nil || s->op == CHANSND);
    assert(r == nil || r->op == CHANRCV);

    /* Channel is empty (or unbuffered) - copy directly.
     * 如果 s/r 来自通道内部, 他们一定是来自暂存区而不是缓存区
     *
	 * 如果通道是空通道(或者没有缓冲区), 直接从 s/r 里面读写就好了 */
    if (s && r && c->nbuf == 0) {
        amove(r->v, s->v, c->elemsize);
        return;
    }

    /* altcanexec 能保证 c->nbuf 不为空, 下面放心大胆地使用它 */

    /* 在缓存区有数据的情况下, 从缓存区读取数据
	 * Otherwise it's always okay to receive and then send. */
    if (r) {
        cp = c->buf + c->off * c->elemsize;
        amove(r->v, cp, c->elemsize);
        --c->nbuf; /* 标识消耗掉一个数据 */

        /* 回环, 如果 offset 增长到缓冲区大小, 令他从 0 开始继续 */
        if (++c->off == c->bufsize)
            c->off = 0;
    }

    /* 在有缓存区的情况下, 从 s 读取出来数据, 放置到缓存区 */
    if (s) {
        /* 环形缓存区, 不判断溢出是因为 altcanexec 保驾护航, 这里一定不会溢出 */
        cp = c->buf + (c->off + c->nbuf) % c->bufsize * c->elemsize;
        amove(cp, s->v, c->elemsize);
        ++c->nbuf;
    }
}

/* 真正的执行 a[0]->op 操作
 * 本函数调用之前已经做了 altcanexec 检查 */
static void altexec(Alt *a) {
    int i;
    Altarray *ar;
    Alt *other;
    Channel *c;

    c = a->c;

    /* 取出来 a->op 操作需要使用的 channel 暂存区 */
    ar = chanarray(c, otherop(a->op));

    if (ar && ar->n) {
        /* 走到这里意味着有某个 coroutine 正在阻塞等待 op 相反操作的数据, 这样 op 和它就可以相互成全了(一个接一个发)
         * 有多个 coroutine 都在阻塞等待, 他们拿到的数据是不保证顺序的 */

        /* 随机从可以相互成全的队列里面取一个, 开始 a->op/not(a->op) 操作
         * other 就是 a->op 的对手, 比如 op = CHANSND 对应 arecv 暂存区的元素, op = CHANRCV 对应 asend 暂存区的元素 */
        i = rand() % ar->n;
        other = ar->a[i];

        /* 根据 a->op, 将 other 拷贝到 a, 或者将 a 拷贝到 other. 完成这个互相成全的过程 */
        altcopy(a, other);

        altalldequeue(other->xalt);  /* 此时 other 对应的动作已经执行完成, 将它从暂存区里面剔除 */
        other->xalt[0].xalt = other; /* 这个赋值操作主要是为了 chanalt 函数能正确的返回大于 0 的值表示自己执行成功了 */

        taskready(other->task); /* 对手协程已经完成读取/发送数据, 将它标记为 READY, 后面可以继续被调度执行 */
    } else {
        /* 这里是没有暂存队列的情况, 没有暂存队列就意味着自己没有对手操作, 这样就要依赖缓冲区
         * altcanexec 会保证缓冲区一定可用 */
        altcopy(a, nil);
    }
}

/* 返回 0 表示执行成功 */
int chanalt(Alt *a) {
    int i, j, ncan, n, canblock;
    Channel *c;
    Task *t;

    needstack(512);
    for (i = 0; a[i].op != CHANEND && a[i].op != CHANNOBLK; i++)
        ;
    n = i;
    canblock = a[i].op == CHANEND;

    t = taskrunning;
    for (i = 0; i < n; i++) {
        a[i].task = t;
        a[i].xalt = a; /* xalt 将来用于完成 op 之后, 清理暂存使用 */
    }
    ncan = 0;
    for (i = 0; i < n; i++) {
        c = a[i].c;
        if (altcanexec(&a[i])) {
            ncan++;
        }
    }

    if (ncan) {
        j = rand() % ncan;
        for (i = 0; i < n; i++) {
            if (altcanexec(&a[i])) {
                if (j-- == 0) {
                    altexec(&a[i]);
                    return i;
                }
            }
        }
    }

    /* 不允许阻塞又无法操作的 case, 数据就丢失了
     * 1. 无缓存区, 使用 non-block API 操作通道
     * 2. 有缓存区, 但是缓存区满了  */
    if (!canblock)
        return -1;

    /* 允许阻塞的情况, 将数据放到暂存区, 切出任务的执行(阻塞效果)
     * 阻塞发送/阻塞获取都可以追加到相应的暂存区里面
     */
    for (i = 0; i < n; i++) {
        if (a[i].op != CHANNOP)
            altqueue(&a[i]); /* 看这里将全部的 a 操作元素都压进队列里面去了 */
    }

    taskswitch();

    /* the guy who ran the op took care of dequeueing us
	 * and then set a[0].alt to the one that was executed. */
    return a[0].xalt - a;
}

static int _chanop(Channel *c, int op, void *p, int canblock) {
    Alt a[2];

    a[0].c = c;
    a[0].op = op;
    a[0].v = p;
    a[1].op = canblock ? CHANEND : CHANNOBLK;
    if (chanalt(a) < 0)
        return -1;
    return 1;
}

int chansend(Channel *c, void *v) {
    return _chanop(c, CHANSND, v, 1);
}

int channbsend(Channel *c, void *v) {
    return _chanop(c, CHANSND, v, 0);
}

int chanrecv(Channel *c, void *v) {
    return _chanop(c, CHANRCV, v, 1);
}

int channbrecv(Channel *c, void *v) {
    return _chanop(c, CHANRCV, v, 0);
}

int chansendp(Channel *c, void *v) {
    return _chanop(c, CHANSND, (void *)&v, 1);
}

void *chanrecvp(Channel *c) {
    void *v;

    _chanop(c, CHANRCV, (void *)&v, 1);
    return v;
}

int channbsendp(Channel *c, void *v) {
    return _chanop(c, CHANSND, (void *)&v, 0);
}

void *channbrecvp(Channel *c) {
    void *v;

    _chanop(c, CHANRCV, (void *)&v, 0);
    return v;
}

int chansendul(Channel *c, ulong val) {
    return _chanop(c, CHANSND, &val, 1);
}

ulong chanrecvul(Channel *c) {
    ulong val;

    _chanop(c, CHANRCV, &val, 1);
    return val;
}

int channbsendul(Channel *c, ulong val) {
    return _chanop(c, CHANSND, &val, 0);
}

ulong channbrecvul(Channel *c) {
    ulong val;

    _chanop(c, CHANRCV, &val, 0);
    return val;
}
