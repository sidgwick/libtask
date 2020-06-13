/* 参考资料:
 * esp 寄存器与参数和返回值: https://stackoverflow.com/questions/3699283/what-is-stack-frame-in-assembly/3700219#3700219

 */

.globl getcontext
getcontext:
  movl 4(%esp), %eax

  mov  %fs, 36(%eax)
  mov  %es, 38(%eax)
  mov  %ds, 40(%eax)
  mov  %ss, 42(%eax)
  movl  %edi, (%eax)
  movl  %esi, 4(%eax)
  movl  %ebp, 8(%eax)
  movl  %ebx, 12(%eax)
  movl  %edx, 16(%eax)
  movl  %ecx, 20(%eax)

  /* 设置将来 setcontext 之后的 %eax 值为 1, 这个立即数 1 貌似没有什么含义 */
  movl  $1, 32(%eax)

  /* 设置将来 setcontext 之后的 %eip */
  movl (%esp), %ecx /* %esp 指向的内存地址是 getcontext 函数的 */
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
