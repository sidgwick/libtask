#include "context.h"

#include <stdio.h>
#include <stdlib.h>

int main() {
    scontext_t *ctx = malloc(sizeof(scontext_t));

    int i = 0;
    printf("before context set\n");
    getcontext(&ctx->mcontext);
    printf("after context set\n");

    if (i < 5) {
        printf("context change test: %d\n", i);
        i++;

        setcontext(&ctx->mcontext);
    }

    printf("context change test done.\n");
    return 0;
}