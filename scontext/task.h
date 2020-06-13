#include <stdint.h>
#include <stdlib.h>

typedef struct __scontext scontext_t;
typedef struct __smcontext smcontext_t;
typedef struct __stack stack_t;

/* 表示 CPU 寄存器的结构体 */
struct __smcontext {
    uint32_t edi;  // 0
    uint32_t esi;  // 4
    uint32_t ebp;  // 8
    uint32_t ebx;  // 12
    uint32_t edx;  // 16
    uint32_t ecx;  // 20
    uint32_t esp;  // 24
    uint32_t eip;  // 28
    uint32_t eax;  // 32

    uint16_t fs;  // 36
    uint16_t es;  // 38
    uint16_t ds;  // 40
    uint16_t ss;  // 42
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
int setcontext(const smcontext_t *);

// int setcontext(smcontext_t *cpu) {
//     cpu->es = 1;
// }

// int getcontext(smcontext_t *cpu) {
//     int x = cpu->es;
// }
