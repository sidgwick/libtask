#define _XOPEN_SOURCE

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>

int main() {
    sucontext_t uc;
    int i = 0;
    printf("before getcontext\n");
    getcontext(&uc);
    printf("after getcontext\n");

    i++;
    if (i < 5) {
        int y = setcontext(&uc);
    }

    printf("setcontext finished\n");
    return 0;
}

// int test_abca(int a, int b) {
//     int aa = a;
//     int bb = b;

//     int c = bb + aa;

//     return c;
// }

// int test_abc(int a, int b) {
//     int aa = a;
//     int bb = b;

//     int c = bb + aa;

//     int x = test_abca(aa, c);

//     return c + x;
// }

// int main() {
//     int x = 22;
//     int y = 33;
//     int z = test_abc(x, y);
//     return z;
// }
