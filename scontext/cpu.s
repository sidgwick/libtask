/* 参考资料:
 * esp 寄存器与参数和返回值: https://stackoverflow.com/questions/3699283/what-is-stack-frame-in-assembly/3700219#3700219
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

  /* 设置将来 setcontext 之后的 %eax 值为 1, 这个立即数 1 貌似没有什么含义 */
  movl  $1, 32(%eax)

  /* 设置将来 setcontext 之后的 %eip
   * callq 指令会将 getcontext 调用时候的下一条指令地址压入 (%esp)
   */
  movl (%esp), %ecx
  movl %ecx, 28(%eax)

  /* 设置将来 setcontext 的 %esp */
  leal 4(%esp), %ecx
  movl %ecx, 24(%eax) /* %esp, 栈指针 --> getcontext 函数出口处的栈指针位置 */

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
  movl  32(%eax), %eax
  ret
