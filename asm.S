/* Copyright (c) 2005-2006 Russ Cox, MIT; see COPYRIGHT */

/*
struct mcontext {
    int mc_onstack;    // 0
    int mc_rdi;        // 4
    int mc_rsi;        // 8
    int mc_rdx;        // 12
    int mc_rcx;        // 16
    int mc_r8;         // 20
    int mc_r9;         // 24
    int mc_rax;        // 28
    int mc_rbx;        // 32
    int mc_rbp;        // 36
    int mc_r10;        // 40
    int mc_r11;        // 44
    int mc_r12;        // 48
    int mc_r13;        // 52
    int mc_r14;        // 56
    int mc_r15;        // 60
    int mc_trapno;     // 64
    int mc_addr;       // 68
    int mc_flags;      // 72
    int mc_err;        // 76
    int mc_rip;        // 80
    int mc_cs;         // 84
    int mc_rflags;     // 88
    int mc_rsp;        // 92
    int mc_ss;         // 96

	....
};

 * 调用形式如下:
 * uc_mctx = u->uc_mcontext
 * setmcontext(&uc_mctx)
 */
.globl setmcontext
setmcontext:
	movl	4(%esp), %eax /* %eax 现在是 &uc_mctx */

	mov	8(%eax), %fs
	mov	12(%eax), %es
	mov	16(%eax), %ds
	mov	76(%eax), %ss
	movl	20(%eax), %edi
	movl	24(%eax), %esi
	movl	28(%eax), %ebp
	movl	36(%eax), %ebx
	movl	40(%eax), %edx
	movl	44(%eax), %ecx

	movl	72(%eax), %esp /* 设置运行时栈底 */
	pushl	60(%eax)	/* new %eip, 设置指令指针 %eip */
	movl	48(%eax), %eax /* 设置 %eax, 这是 setmcontext 的返回值 */
	ret

/*
 * 调用形式如下:
 * uc_mctx = u->uc_mcontext
 * getmcontext(&uc_mctx)
 */
.globl getmcontext
getmcontext:
	movl	4(%esp), %eax /* --> 将 getmcontext 函数的参数 &uc_mctx 推送到 %eax 寄存器 */

	/* (%eax) = mc_onstack
	 * 4(%eax) = mc_rdi
	 */
	
	/* 下面先拿到 10 个寄存器的内容, 放到内存里 */
	mov	%fs, 8(%eax) /* %fs --> mc_rsi */
	mov	%es, 12(%eax) /* %es --> mc_rdx */
	mov	%ds, 16(%eax) /* %ds --> mc_rcx */
	mov	%ss, 76(%eax) /* %ss --> mc_err */
	movl	%edi, 20(%eax) /* %edi --> mc_r8 */
	movl	%esi, 24(%eax) /* %esi --> mc_r9 */
	movl	%ebp, 28(%eax) /* %ebp --> mc_rax */
	movl	%ebx, 36(%eax) /* %ebx --> mc_rbp */
	movl	%edx, 40(%eax) /* %edx --> mc_r10 */
	movl	%ecx, 44(%eax) /* %ecx --> mc_r11 */


	movl	$1, 48(%eax)	/* %eax, --> mc_r12, $1 将来用于 setmcontext 操作的返回值 */
	
	/* 保存 %esp 对应地址的内存数据到 %ecx, 然后保存到 %eax + 60 的位置上 */
	movl	(%esp), %ecx	/* %eip, (%esp) 指向当前栈帧的栈底 */
	movl	%ecx, 60(%eax) /* %eip --> mc_r15 */

	leal	4(%esp), %ecx	/* %esp, 4(%esp) 对应的的是参数 &ut_mctx, 所以这个操作就是将 &ut_mctx 对应的内存地址加载到 %ecx 中(TODO: 保存了二级指针??) */
	movl	%ecx, 72(%eax) /* %esp --> mc_flags */

	movl	44(%eax), %ecx	/* restore %ecx */

	movl	$0, %eax /* 设定 setmcontext 的返回值 */
	ret