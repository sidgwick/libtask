/* Copyright (c) 2005 Russ Cox, MIT; see COPYRIGHT */

#include <stdio.h>
#include <stdlib.h>
#include <task.h>
#include <unistd.h>

int quiet;
int goal;
int buffer;

void primetask(void *arg) {
    Channel *c, *nc;
    int p, i;

    c = arg; /* 通道是通过参数传递进来的 */

    p = chanrecvul(c); /* 从通道里面获取一个 unsigned long 数据 */

    if (p > goal) {
        /* 通道里的数据已经大于 goal 了, 就结束程序.
         * coroutine 并没有退出, 程序结束时依然都在跑着 */
        taskexitall(0);
    }

    /* 非安静模式打印同道中人的数据 */
    if (!quiet)
        printf("get data from channel: %d\n", p);

    /* 再创建一个协程, 使用新的通道通讯 */
    nc = chancreate(sizeof(unsigned long), buffer);
    taskcreate(primetask, nc, 32768);

    /* 死循环遍历处理 */
    for (;;) {
        /* 从 c 里面获取数据, 使用 i % p 计算数据是否可能满足素数条件
         *
         * primetask 会打印出来自己收到 channel 里面的第一个元素(收到它意味着在前几轮嵌套调用里面没有被整除掉, 这个数一定是素数).
         * 根据 primetask 协程的调用深度, 每次 channel 中的数据如下:
         * 0. primetask ==> nc 中数据: 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14...
         * 1. primetask -> primetask ==> nc 中数据(0中不能被2整除的数据): 3, 5, 7, 9, 11, 13, 15....
         * 2. primetask -> primetask -> primetask ==> nc 中数据(1中不能被3整除的数据): 5, 7, 11, 13 ....
         * 3. primetask -> primetask -> primetask -> primetask ==> nc 中数据(2中不能被5整除的数据): 7, 11, 13 ....
         * 4. ...
         */
        i = chanrecvul(c);

        if (i % p)
            chansendul(nc, i);
    }
}

/* 主 coroutine 的入口函数 */
void taskmain(int argc, char **argv) {
    int i;
    Channel *c; /* 通道, 用于向子协程发送数据 */

    if (argc > 1)
        goal = atoi(argv[1]);
    else
        goal = 100;

    printf("goal=%d, buffer=%d\n", goal, buffer);

    c = chancreate(sizeof(unsigned long), buffer);

    taskcreate(primetask, c, 32768);

    /* 在这个死循环里面向通道里面发送数据
     * 这个函数是在调度器里面调度的, 没法有返回值, 所以这里是个死循环 */
    for (i = 2;; i++) {
        /* 向通道中发送的是一系列自然数 */
        /* 这里不能用非阻塞形式, 否则让不出来 CPU, 其他携程无法被调度 */
        chansendul(c, i);
    }
}

void *emalloc(unsigned long n) {
    return calloc(n, 1);
}

long lrand(void) {
    return rand();
}
