.globl getcontext
getcontext:
  movl 4(%esp), %eax

  mov  %fs, (%eax)
  mov  %es, 4(%eax)
  mov  %ds, 8(%eax)
  mov  %ss, 12(%eax)
  movl  %edi, 16(%eax)
  movl  %esi, 20(%eax)
  movl  %ebp, 24(%eax)
  movl  %ebx, 28(%eax)
  movl  %edx, 32(%eax)
  movl  %ecx, 36(%eax)
 
  movl  $1, 48(%eax)

  movl (%esp), %ecx
  movl %ecx, 44(%eax)
 
  leal 4(%esp), %ecx
  movl %ecx, 40(%eax)
 
  movl 36(%eax), %ecx  /* restore %ecx */
 
  movl $0, %eax
  ret

.globl setcontext
setcontext:
  movl  4(%esp), %eax

  mov  (%eax), %fs
  mov  4(%eax), %es
  mov  8(%eax), %ds
  mov  12(%eax), %ss

  movl  16(%eax), %edi
  movl  20(%eax), %esi
  movl  24(%eax), %ebp
  movl  28(%eax), %ebx
  movl  32(%eax), %edx
  movl  36(%eax), %ecx

  movl  40(%eax), %esp
  pushl  44(%eax) /* %eip */
  movl  48(%eax), %eax
  ret
