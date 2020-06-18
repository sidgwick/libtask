#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <task.h>
#include <unistd.h>

enum {
    STACK = 32768
};

char *server;
char *url;

void fetchtask(void *);

void yield_block_fdtask(void *v) {
    for (int i; i < 100; i++) {
        printf("yield_block_fdtask: %d\n", i);
        sleep(1);
        taskyield();
    }
}

void taskmain(int argc, char **argv) {
    int i, n;

    if (argc != 4) {
        fprintf(stderr, "usage: httpload n server url\n");
        taskexitall(1);
    }

    n = atoi(argv[1]);
    server = argv[2];
    url = argv[3];

    // taskcreate(yield_block_fdtask, 0, STACK);

    for (i = 0; i < n; i++) {
        taskcreate(fetchtask, 0, STACK);

        /* 让出资源, 让 fetchtask 先执行
         * 1 意味着除了刚刚创建的 fetchtask 协程之外还有其他协程在执行 */
        while (taskyield() > 1)
            ;
        sleep(1);
    }
}

void fetchtask(void *v) {
    int fd, n;
    char buf[512];

    fprintf(stderr, "starting...\n");
    for (;;) {
        if ((fd = netdial(TCP, server, 80)) < 0) {
            fprintf(stderr, "dial %s: %s (%s)\n", server, strerror(errno), taskgetstate());
            continue;
        }

        snprintf(buf, sizeof buf, "GET %s HTTP/1.0\r\nHost: %s\r\n\r\n", url, server);
        fdwrite(fd, buf, strlen(buf));
        while ((n = fdread(fd, buf, sizeof buf)) > 0) {
            write(1, buf, n);
        }

        close(fd);
        write(1, ".", 1);
    }
}
