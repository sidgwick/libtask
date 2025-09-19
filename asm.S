/* Copyright (c) 2005-2006 Russ Cox, MIT; see COPYRIGHT */

/* 文件做了精简, 只研究 i386 平台上的实现 */

/**
 * @brief 更新 CPU 的寄存器的值, 并跳转到新 ip 执行指令
 *
 * 调用形式如下:
 * ```c
 * ctx = u->uc_xmcontext
 * setxmcontext(&ctx)
 * ```
 *
 * 恢复寄存器的操作直接恢复了 getxmcontext 时刻的信息, 因此最后的 ret 指令,
 * 其实是相当于从 getxmcontext 里面返回了, 这里的返回值(%eax)已经被设置为 1
 *
 * @param xmcontext_t* ctx 上下文指针
 * @return xmcontext_t* 本函数返回传入的 ctx
 */
.globl setxmcontext
setxmcontext:
    movl    4(%esp), %eax /* %eax = ctx */

    movw    8(%eax), %fs
    movw    12(%eax), %es
    movw    16(%eax), %ds
    movw    76(%eax), %ss
    movl    20(%eax), %edi
    movl    24(%eax), %esi
    movl    28(%eax), %ebp
    movl    36(%eax), %ebx
    movl    40(%eax), %edx
    movl    44(%eax), %ecx

    movl    72(%eax), %esp

    /* 设置指令指针 %eip 之后, setxmcontext 自己不能正常返回(它返回到的是 getxmcontext 的返回地址)
     * 最后设置 %eax, 但是 mc_eax 早在 setxmcontext 的时候已经置 1, 因此这里的返回值就是 1
     */

    pushl    60(%eax)    /* new %eip */
    movl    48(%eax), %eax
    ret

 /**
 * @brief 保存 CPU 的寄存器的值, 返回保存结果
 *
 * 调用形式如下:
 * ```c
 * ctx = u->uc_xmcontext
 * getxmcontext(&ctx)
 * ```
 *
 * @param xmcontext_t *ctx 上下文指针
 * @return int 保存完成返回 0
 */
.globl getxmcontext
getxmcontext:
    movl    4(%esp), %eax /* %eax = ctx */

    /* 下面先拿到 10 个寄存器的内容, 放到内存里 */
    movw    %fs, 8(%eax)
    movw    %es, 12(%eax)
    movw    %ds, 16(%eax)
    movw    %ss, 76(%eax)
    movl    %edi, 20(%eax)
    movl    %esi, 24(%eax)
    movl    %ebp, 28(%eax)
    movl    %ebx, 36(%eax)
    movl    %edx, 40(%eax)
    movl    %ecx, 44(%eax)

    /* 把 %eax 置 1, 将来从 setxmcontext 冒充自 getxmcontext 的返回的时候, 返回值就是 1 */
    movl    $1, 48(%eax)    /* %eax */

    /* 保存返回此函数后继续执行的下一条指令地址 EIP */
    movl    (%esp), %ecx    /* %eip */
    movl    %ecx, 60(%eax)

    /* 保存 ctx 参数的内存位置, 这个位置是 getxmcontext 调用之前, %esp 的值 */
    leal    4(%esp), %ecx    /* %esp */
    movl    %ecx, 72(%eax)

    movl    44(%eax), %ecx    /* restore %ecx */
    movl    $0, %eax
    ret
