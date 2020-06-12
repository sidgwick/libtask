/* Copyright (c) 2005-2006 Russ Cox, MIT; see COPYRIGHT */

#if defined(__FreeBSD__) && defined(__i386__) && __FreeBSD__ < 5
#define NEEDX86CONTEXT 1
#define SET setmcontext
#define GET getmcontext
#endif

#if defined(__OpenBSD__) && defined(__i386__)
#define NEEDX86CONTEXT 1
#define SET setmcontext
#define GET getmcontext
#endif

#if defined(__APPLE__)
#if defined(__i386__)
#define NEEDX86CONTEXT 1
#define SET _setmcontext
#define GET _getmcontext
#elif defined(__x86_64__)
#define NEEDAMD64CONTEXT 1
#define SET _setmcontext
#define GET _getmcontext
#else
#define NEEDPOWERCONTEXT 1
#define SET __setmcontext
#define GET __getmcontext
#endif
#endif

#if defined(__linux__) && defined(__arm__)
#define NEEDARMCONTEXT 1
#define SET setmcontext
#define GET getmcontext
#endif

#if defined(__linux__) && defined(__mips__)
#define NEEDMIPSCONTEXT 1
#define SET setmcontext
#define GET getmcontext
#endif

#ifdef NEEDX86CONTEXT

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
.globl SET
SET:
	movl	4(%esp), %eax /* %eax 现在是 &uc_mctx */

	movl	8(%eax), %fs
	movl	12(%eax), %es
	movl	16(%eax), %ds
	movl	76(%eax), %ss
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
.globl GET
GET:
	movl	4(%esp), %eax /* --> 将 getmcontext 函数的参数 &uc_mctx 推送到 %eax 寄存器 */

	/* (%eax) = mc_onstack
	 * 4(%eax) = mc_rdi
	 */
	
	/* 下面先拿到 10 个寄存器的内容, 放到内存里 */
	movl	%fs, 8(%eax) /* %fs --> mc_rsi */
	movl	%es, 12(%eax) /* %es --> mc_rdx */
	movl	%ds, 16(%eax) /* %ds --> mc_rcx */
	movl	%ss, 76(%eax) /* %ss --> mc_err */
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
#endif

#ifdef NEEDAMD64CONTEXT
.globl SET
SET:
	movq	16(%rdi), %rsi
	movq	24(%rdi), %rdx
	movq	32(%rdi), %rcx
	movq	40(%rdi), %r8
	movq	48(%rdi), %r9
	movq	56(%rdi), %rax
	movq	64(%rdi), %rbx
	movq	72(%rdi), %rbp
	movq	80(%rdi), %r10
	movq	88(%rdi), %r11
	movq	96(%rdi), %r12
	movq	104(%rdi), %r13
	movq	112(%rdi), %r14
	movq	120(%rdi), %r15
	movq	184(%rdi), %rsp
	pushq	160(%rdi)	/* new %eip */
	movq	8(%rdi), %rdi
	ret

.globl GET
GET:
	movq	%rdi, 8(%rdi)
	movq	%rsi, 16(%rdi)
	movq	%rdx, 24(%rdi)
	movq	%rcx, 32(%rdi)
	movq	%r8, 40(%rdi)
	movq	%r9, 48(%rdi)
	movq	$1, 56(%rdi)	/* %rax */
	movq	%rbx, 64(%rdi)
	movq	%rbp, 72(%rdi)
	movq	%r10, 80(%rdi)
	movq	%r11, 88(%rdi)
	movq	%r12, 96(%rdi)
	movq	%r13, 104(%rdi)
	movq	%r14, 112(%rdi)
	movq	%r15, 120(%rdi)

	movq	(%rsp), %rcx	/* %rip */
	movq	%rcx, 160(%rdi)
	leaq	8(%rsp), %rcx	/* %rsp */
	movq	%rcx, 184(%rdi)
	
	movq	32(%rdi), %rcx	/* restore %rcx */
	movq	$0, %rax
	ret
#endif

#ifdef NEEDPOWERCONTEXT
/* get FPR and VR use flags with sc 0x7FF3 */
/* get vsave with mfspr reg, 256 */

.text
.align 2

.globl GET
GET:				/* xxx: instruction scheduling */
	mflr	r0
	mfcr	r5
	mfctr	r6
	mfxer	r7
	stw	r0, 0*4(r3)
	stw	r5, 1*4(r3)
	stw	r6, 2*4(r3)
	stw	r7, 3*4(r3)

	stw	r1, 4*4(r3)
	stw	r2, 5*4(r3)
	li	r5, 1			/* return value for setmcontext */
	stw	r5, 6*4(r3)

	stw	r13, (0+7)*4(r3)	/* callee-save GPRs */
	stw	r14, (1+7)*4(r3)	/* xxx: block move */
	stw	r15, (2+7)*4(r3)
	stw	r16, (3+7)*4(r3)
	stw	r17, (4+7)*4(r3)
	stw	r18, (5+7)*4(r3)
	stw	r19, (6+7)*4(r3)
	stw	r20, (7+7)*4(r3)
	stw	r21, (8+7)*4(r3)
	stw	r22, (9+7)*4(r3)
	stw	r23, (10+7)*4(r3)
	stw	r24, (11+7)*4(r3)
	stw	r25, (12+7)*4(r3)
	stw	r26, (13+7)*4(r3)
	stw	r27, (14+7)*4(r3)
	stw	r28, (15+7)*4(r3)
	stw	r29, (16+7)*4(r3)
	stw	r30, (17+7)*4(r3)
	stw	r31, (18+7)*4(r3)

	li	r3, 0			/* return */
	blr

.globl SET
SET:
	lwz	r13, (0+7)*4(r3)	/* callee-save GPRs */
	lwz	r14, (1+7)*4(r3)	/* xxx: block move */
	lwz	r15, (2+7)*4(r3)
	lwz	r16, (3+7)*4(r3)
	lwz	r17, (4+7)*4(r3)
	lwz	r18, (5+7)*4(r3)
	lwz	r19, (6+7)*4(r3)
	lwz	r20, (7+7)*4(r3)
	lwz	r21, (8+7)*4(r3)
	lwz	r22, (9+7)*4(r3)
	lwz	r23, (10+7)*4(r3)
	lwz	r24, (11+7)*4(r3)
	lwz	r25, (12+7)*4(r3)
	lwz	r26, (13+7)*4(r3)
	lwz	r27, (14+7)*4(r3)
	lwz	r28, (15+7)*4(r3)
	lwz	r29, (16+7)*4(r3)
	lwz	r30, (17+7)*4(r3)
	lwz	r31, (18+7)*4(r3)

	lwz	r1, 4*4(r3)
	lwz	r2, 5*4(r3)

	lwz	r0, 0*4(r3)
	mtlr	r0
	lwz	r0, 1*4(r3)
	mtcr	r0			/* mtcrf 0xFF, r0 */
	lwz	r0, 2*4(r3)
	mtctr	r0
	lwz	r0, 3*4(r3)
	mtxer	r0

	lwz	r3,	6*4(r3)
	blr
#endif

#ifdef NEEDARMCONTEXT
.globl GET
GET:
	str	r1, [r0,#4]
	str	r2, [r0,#8]
	str	r3, [r0,#12]
	str	r4, [r0,#16]
	str	r5, [r0,#20]
	str	r6, [r0,#24]
	str	r7, [r0,#28]
	str	r8, [r0,#32]
	str	r9, [r0,#36]
	str	r10, [r0,#40]
	str	r11, [r0,#44]
	str	r12, [r0,#48]
	str	r13, [r0,#52]
	str	r14, [r0,#56]
	/* store 1 as r0-to-restore */
	mov	r1, #1
	str	r1, [r0]
	/* return 0 */
	mov	r0, #0
	mov	pc, lr

.globl SET
SET:
	ldr	r1, [r0,#4]
	ldr	r2, [r0,#8]
	ldr	r3, [r0,#12]
	ldr	r4, [r0,#16]
	ldr	r5, [r0,#20]
	ldr	r6, [r0,#24]
	ldr	r7, [r0,#28]
	ldr	r8, [r0,#32]
	ldr	r9, [r0,#36]
	ldr	r10, [r0,#40]
	ldr	r11, [r0,#44]
	ldr	r12, [r0,#48]
	ldr	r13, [r0,#52]
	ldr	r14, [r0,#56]
	ldr	r0, [r0]
	mov	pc, lr
#endif

#ifdef NEEDMIPSCONTEXT
.globl GET
GET:
	sw	$4, 24($4)
	sw	$5, 28($4)
	sw	$6, 32($4)
	sw	$7, 36($4)

	sw	$16, 72($4)
	sw	$17, 76($4)
	sw	$18, 80($4)
	sw	$19, 84($4)
	sw	$20, 88($4)
	sw	$21, 92($4)
	sw	$22, 96($4)
	sw	$23, 100($4)

	sw	$28, 120($4)	/* gp */
	sw	$29, 124($4)	/* sp */
	sw	$30, 128($4)	/* fp */
	sw	$31, 132($4)	/* ra */

	xor	$2, $2, $2
	j	$31
	nop

.globl SET
SET:
	lw	$16, 72($4)
	lw	$17, 76($4)
	lw	$18, 80($4)
	lw	$19, 84($4)
	lw	$20, 88($4)
	lw	$21, 92($4)
	lw	$22, 96($4)
	lw	$23, 100($4)

	lw	$28, 120($4)	/* gp */
	lw	$29, 124($4)	/* sp */
	lw	$30, 128($4)	/* fp */
	
	/* 
	 * If we set $31 directly and j $31, 
	 * we would loose the outer return address.
	 * Use a temporary register, then.
	 */
	lw	$8, 132($4)		/* ra */
		
	/*  bug: not setting the pc causes a bus error */
	lw	$25, 132($4)	/* pc */

	lw	$5, 28($4)
	lw	$6, 32($4)
	lw	$7, 36($4)
	lw	$4, 24($4)

	j	$8
	nop
#endif
