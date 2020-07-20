/* 参考资料:
 * esp 寄存器与参数和返回值: https://stackoverflow.com/questions/3699283/what-is-stack-frame-in-assembly/3700219#3700219
 * C 函数的栈帧结构: https://blog.csdn.net/xbb224007/article/details/80106961
 * 这两段汇编很好懂, 理解清楚 C 语言函数调用的栈帧结构, 一眼就能看明白. 理不清楚栈帧结构怎么看都不会明白
 *
 * 32 位机器上的 swapcontext 调用 getcontext 时候的栈帧布局
 *
 *               |                                             |
 *  (%ebp)   ->  |                                             |
 *               |                                             |
 *               |                                             |
 *  4(%esp)  ->  |  address of ctx paramenet                   |
 *  (%esp)   ->  |  next instruction of swapcontext function   |
 *          ------------------------------------------------------
 *  -4(%esp) ->  |                                             |
 *  -8(%esp) ->  |                                             |
 *               |                                             |
 *               |                                             |
 *               |                                             |
 *               |                                             |
 *               |                                             |
 *
 * > 补充知识
 * > **注意**下面提到的这部分在 getcontext/setcontext 汇编里面没有体现, 这两个函数使用的还是 swapcontext 的栈帧.
 * 如果是编译器编译出来的, 这里的 -4(%esp) 将会存放 swapcontext 的栈底, 也就是上面的 (%ebp) 对应内存地址
 * 之后编译器计算函数运行过程中需要多大的运行时栈, 直接将 %esp 寄存器设置到能满足栈空间大小的位置.
 * 然后内存布局变成下面这样子:
 *
 *               |                                             |
 *           ->  |                                             |
 *               |                                             |
 *               |                                             |
 *           ->  |  address of ctx paramenet                   |
 *           ->  |  next instruction of swapcontext function   |
 *          ------------------------------------------------------
 *  (%ebp)   ->  |   caller function stack base                |
 *           ->  |                                             |
 *               |                                             |
 *               |                                             |
 *               |                                             |
 *  (%esp)   ->  |                                             |
 *               |                                             |
 *
 * > 参考:
 * https://www.cnblogs.com/friedCoder/articles/12374666.html
 * https://blog.csdn.net/liutianshx2012/article/details/50974839
 * https://stackoverflow.com/questions/4228261/understanding-the-purpose-of-some-assembly-statements
 *
 * swapcontext 函数的汇编代码
 * > gcc -m32 -fno-pie -O0 -c task.c
 * > objdump -d task.o
 * 00000000 <swapcontext>:
 *   0:	55                   	push   %ebp
 *   1:	89 e5                	mov    %esp,%ebp
 *   3:	83 ec 18             	sub    $0x18,%esp  -------> 开辟了 0x18 = 24 字节的栈空间
 *
 *   6:	83 ec 0c             	sub    $0xc,%esp   -------> 栈空间 +0xc = 12 字节, 现在栈空间共计 36 字节(多余的 12 字节用于: 1 参数 + 1 下一条指令 + 1 新函数栈帧栈底)
 *   9:	ff 75 08             	pushl  0x8(%ebp)   -------> 0x8(%ebp) 入栈, 这里实际上就是 from 参数
 *   c:	e8 fc ff ff ff       	call   d <swapcontext+0xd>
 *
 *  11:	83 c4 10             	add    $0x10,%esp  -------> 栈空间 -0x10 = 16 字节, 现在栈空间共计 16 字节
 *
 *  14:	89 45 f4             	mov    %eax,-0xc(%ebp)  ---> %eax 里面是 getcontext 的返回值, 现在把它存到内存中, 位置在 -0xc(%ebp)
 *  17:	83 7d f4 00          	cmpl   $0x0,-0xc(%ebp)  ----> 比较返回值
 *  1b:	75 0e                	jne    2b <swapcontext+0x2b>  ---> 不相等跳转到 2b 位置, 直接就退出函数了
 *
 *  1d:	83 ec 0c             	sub    $0xc,%esp   -------> 栈空间 +12 字节, 原因同上文, 现在栈空间有 28 字节
 *  20:	ff 75 0c             	pushl  0xc(%ebp)   -------> to 参数入栈
 *  23:	e8 fc ff ff ff       	call   24 <swapcontext+0x24>   ------> 调用函数 setcontext
 *
 *  28:	83 c4 10             	add    $0x10,%esp   -------> 栈空间 -16 字节, 现在栈空间有 12 字节
 *
 *  2b:	b8 00 00 00 00       	mov    $0x0,%eax    ------> 返回值
 *  30:	c9                   	leave
 *  31:	c3                   	ret
 *
 *  对应的 C 代码:
 * int swapcontext(smcontext_t *from, const smcontext_t *to) {
 *     int gr = getcontext(from);
 *     if (gr == 0) {
 *         setcontext(to);
 *     }
 *
 *     return 0;
 * }
 *
 */

.globl getcontext
getcontext:
  movl 4(%esp), %eax

  /* 下面 4 行保存 4 个 16 位的段寄存器到结构体中 */
  mov  %fs, 36(%eax)
  mov  %es, 38(%eax)
  mov  %ds, 40(%eax)
  mov  %ss, 42(%eax)

  /* 接下来保存 9 个 32 位的寄存器 */
  movl  %edi, (%eax)
  movl  %esi, 4(%eax)
  movl  %ebp, 8(%eax)
  movl  %ebx, 12(%eax)
  movl  %edx, 16(%eax)
  movl  %ecx, 20(%eax)

  /* 设置将来 setcontext 之后的 %eax 值为 1, 这个 1 将来也有类似 setcontext 返回值的效果 */
  movl  $1, 32(%eax)

  /* 设置将来 setcontext 之后的 %eip
   * callq 指令会将 getcontext 调用时候的下一条指令地址压入 (%esp)
   * 所以这里的效果就是将来 setcontext 之后从调用该 setcontext 的下一行开始执行
   */
  movl (%esp), %ecx
  movl %ecx, 28(%eax)

  /* 设置将来 setcontext 的 %esp */
  leal 4(%esp), %ecx
  movl %ecx, 24(%eax) /* 4(%esp) = getcontext函数栈帧栈顶指针, leal 指令取该内存地址 */

  /* restore %ecx */
  movl 20(%eax), %ecx

  /* return value is 0 */
  movl $0, %eax
  ret

.globl setcontext
setcontext:
  movl  4(%esp), %eax

  mov  36(%eax), %fs
  mov  38(%eax), %es
  mov  40(%eax), %ds
  mov  42(%eax), %ss

  movl  (%eax), %edi
  movl  4(%eax), %esi
  movl  8(%eax), %ebp
  movl  12(%eax), %ebx
  movl  16(%eax), %edx
  movl  20(%eax), %ecx

  movl  24(%eax), %esp
  pushl  28(%eax) /* %eip */

  /* 请注意这里设置的不是 setcontext  函数本身的返回值, 而是上面 getcontext 的返回值.
   * setcontext 函数比较特殊, 它将自己退出之后的后续指令地址设置为 28(%eax)
   * setcontext 执行之后它后面的所有指令都执行不到, 这就是为什么 swapcontext 函数需要将
   * setcontext 函数用 if 包起来的原因
   *
   * 上面把 %eip 恢复到退出 getcontext 之后的地方了, 因此这里 ret
   * 之后的效果就是以 32(%eax) 作为返回值继续 setcontext 后面的指令执行 */
  movl  32(%eax), %eax

  ret
