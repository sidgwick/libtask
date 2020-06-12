#include <stdint.h>
#include <stdlib.h>

typedef struct __scontext scontext_t;
typedef struct __smcontext smcontext_t;
typedef struct __stack stack_t;

/* 表示 CPU 寄存器的结构体 */
struct __smcontext {
    uint32_t fs;
    uint32_t es;
    uint32_t ds;
    uint32_t ss;

    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t esp;
    uint32_t eip;
    uint32_t eax;
};

/* 表示运行时栈 */
struct __stack {
    void *ss_sp;    /* 运行时栈底 */
    size_t ss_size; /* 运行时栈大小 */
};

/* 表示运行时上下文的结构体 */
struct __scontext {
    smcontext_t mcontext;
    stack_t stack;
};

int getcontext(smcontext_t *);
int setcontext(smcontext_t *);

// int setcontext(smcontext_t *cpu) {
//     cpu->es = 1;
// }

// int getcontext(smcontext_t *cpu) {
//     int x = cpu->es;
// }
