/*
  Copyright 2011, 2012 Kristian Nielsen and Monty Program Ab
            2016 MariaDB Corporation AB

  This file is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this.  If not, see <http://www.gnu.org/licenses/>.
*/

/*
  Implementation of async context spawning using Posix ucontext and
  swapcontext().
*/

#include <stdint.h>
#include <stdlib.h>

#include "ma_global.h"
#include "ma_string.h"
#include "ma_context.h"

#ifdef HAVE_VALGRIND
#include <valgrind/valgrind.h>
#endif

#ifdef MY_CONTEXT_USE_UCONTEXT

typedef void (*uc_func_t)(void);

/*
  The makecontext() only allows to pass integers into the created context :-(
  We want to pass pointers, so we do it this kinda hackish way.
  Anyway, it should work everywhere, and at least it does not break strict
  aliasing.
*/
union pass_void_ptr_as_2_int {
  int a[2];
  void *p;
};

/*
  We use old-style function definition here, as this is passed to
  makecontext(). And the type of the makecontext() argument does not match
  the actual type (as the actual type can differ from call to call).
*/
static void
my_context_spawn_internal(int i0, int i1)
{
  int err;
  struct my_context *c;
  union pass_void_ptr_as_2_int u;

  u.a[0]= i0;
  u.a[1]= i1;
  c= (struct my_context *)u.p;

  (*c->user_func)(c->user_data);
  c->active= 0;
  err= setcontext(&c->base_context);
  fprintf(stderr, "Aieie, setcontext() failed: %d (errno=%d)\n", err, errno);
}


int
my_context_continue(struct my_context *c)
{
  int err;

  if (!c->active)
    return 0;

  err= swapcontext(&c->base_context, &c->spawned_context);
  if (err)
  {
    fprintf(stderr, "Aieie, swapcontext() failed: %d (errno=%d)\n",
            err, errno);
    return -1;
  }

  return c->active;
}


int
my_context_spawn(struct my_context *c, void (*f)(void *), void *d)
{
  int err;
  union pass_void_ptr_as_2_int u;

  err= getcontext(&c->spawned_context);
  if (err)
    return -1;
  c->spawned_context.uc_stack.ss_sp= c->stack;
  c->spawned_context.uc_stack.ss_size= c->stack_size;
  c->spawned_context.uc_link= NULL;
  c->user_func= f;
  c->user_data= d;
  c->active= 1;
  u.a[1]= 0;   /* Otherwise can give uninitialized warnings on 32-bit. */
  u.p= c;
  /*
    makecontext function expects function pointer to receive multiple
    ints as an arguments, however is declared in ucontext.h header with
    a void (empty) argument list. Ignore clang cast-function-type-strict
    warning for this function call.
  */
# ifdef __clang__
#  pragma clang diagnostic push
#  pragma clang diagnostic ignored "-Wcast-function-type-strict"
# endif
  makecontext(&c->spawned_context, (uc_func_t)my_context_spawn_internal, 2,
              u.a[0], u.a[1]);
# ifdef __clang__
#  pragma clang diagnostic pop
# endif

  return my_context_continue(c);
}


int
my_context_yield(struct my_context *c)
{
  int err;

  if (!c->active)
    return -1;

  err= swapcontext(&c->spawned_context, &c->base_context);
  if (err)
    return -1;
  return 0;
}

int
my_context_init(struct my_context *c, size_t stack_size)
{
#if SIZEOF_CHARP > SIZEOF_INT*2
#error Error: Unable to store pointer in 2 ints on this architecture
#endif

  memset(c, 0, sizeof(*c));
  if (!(c->stack= malloc(stack_size)))
    return -1;                                  /* Out of memory */
  c->stack_size= stack_size;
#ifdef HAVE_VALGRIND
  c->valgrind_stack_id=
    VALGRIND_STACK_REGISTER(c->stack, ((unsigned char *)(c->stack))+stack_size);
#endif
  return 0;
}

void
my_context_destroy(struct my_context *c)
{
  if (c->stack)
  {
#ifdef HAVE_VALGRIND
    VALGRIND_STACK_DEREGISTER(c->valgrind_stack_id);
#endif
    free(c->stack);
  }
}

#endif  /* MY_CONTEXT_USE_UCONTEXT */


#ifdef MY_CONTEXT_USE_X86_64_GCC_ASM
/*
  GCC-amd64 implementation of my_context.

  This is slightly optimized in the common case where we never yield
  (eg. fetch next row and it is already fully received in buffer). In this
  case we do not need to restore registers at return (though we still need to
  save them as we cannot know if we will yield or not in advance).
*/

/*
  Layout of saved registers etc.
  Since this is accessed through gcc inline assembler, it is simpler to just
  use numbers than to try to define nice constants or structs.

   0    0   %rsp
   1    8   %rbp
   2   16   %rbx
   3   24   %r12
   4   32   %r13
   5   40   %r14
   6   48   %r15
   7   56   %rip for done
   8   64   %rip for yield/continue
*/

int
my_context_spawn(struct my_context *c, void (*f)(void *), void *d)
{
  int ret;

  /*
    There are 6 callee-save registers we need to save and restore when
    suspending and continuing, plus stack pointer %rsp and instruction pointer
    %rip.

    However, if we never suspend, the user-supplied function will in any case
    restore the 6 callee-save registers, so we can avoid restoring them in
    this case.
  */
  __asm__ __volatile__
    (
     "movq %%rsp, (%[save])\n\t"
     "movq %[stack], %%rsp\n\t"
#if defined(__GCC_HAVE_DWARF2_CFI_ASM) || (defined(__clang__) && __clang_major__ < 13)
     /*
       This emits a DWARF DW_CFA_undefined directive to make the return address
       undefined. This indicates that this is the top of the stack frame, and
       helps tools that use DWARF stack unwinding to obtain stack traces.
       (I use numeric constant to avoid a dependency on libdwarf includes).
     */
     ".cfi_escape 0x07, 16\n\t"
#endif
     "movq %%rbp, 8(%[save])\n\t"
     "movq %%rbx, 16(%[save])\n\t"
     "movq %%r12, 24(%[save])\n\t"
     "movq %%r13, 32(%[save])\n\t"
     "movq %%r14, 40(%[save])\n\t"
     "movq %%r15, 48(%[save])\n\t"
     "leaq 1f(%%rip), %%rax\n\t"
     "leaq 2f(%%rip), %%rcx\n\t"
     "movq %%rax, 56(%[save])\n\t"
     "movq %%rcx, 64(%[save])\n\t"
     /*
       Constraint below puts the argument to the user function into %rdi, as
       needed for the calling convention.
     */
     "callq *%[f]\n\t"
     "jmpq *56(%[save])\n"
     /*
       Come here when operation is done.
       We do not need to restore callee-save registers, as the called function
       will do this for us if needed.
     */
     "1:\n\t"
     "movq (%[save]), %%rsp\n\t"
     "xorl %[ret], %[ret]\n\t"
     "jmp 3f\n"
     /* Come here when operation was suspended. */
     "2:\n\t"
     "movl $1, %[ret]\n"
     "3:\n"
     : [ret] "=a" (ret),
       [f] "+S" (f),
       /* Need this in %rdi to follow calling convention. */
       [d] "+D" (d)
     : [stack] "a" (c->stack_top),
       /* Need this in callee-save register to preserve in function call. */
       [save] "b" (&c->save[0])
     : "rcx", "rdx", "r8", "r9", "r10", "r11", "memory", "cc"
  );

  return ret;
}

int
my_context_continue(struct my_context *c)
{
  int ret;

  __asm__ __volatile__
    (
     "movq (%[save]), %%rax\n\t"
     "movq %%rsp, (%[save])\n\t"
     "movq %%rax, %%rsp\n\t"
     "movq 8(%[save]), %%rax\n\t"
     "movq %%rbp, 8(%[save])\n\t"
     "movq %%rax, %%rbp\n\t"
     "movq 24(%[save]), %%rax\n\t"
     "movq %%r12, 24(%[save])\n\t"
     "movq %%rax, %%r12\n\t"
     "movq 32(%[save]), %%rax\n\t"
     "movq %%r13, 32(%[save])\n\t"
     "movq %%rax, %%r13\n\t"
     "movq 40(%[save]), %%rax\n\t"
     "movq %%r14, 40(%[save])\n\t"
     "movq %%rax, %%r14\n\t"
     "movq 48(%[save]), %%rax\n\t"
     "movq %%r15, 48(%[save])\n\t"
     "movq %%rax, %%r15\n\t"

     "leaq 1f(%%rip), %%rax\n\t"
     "leaq 2f(%%rip), %%rcx\n\t"
     "movq %%rax, 56(%[save])\n\t"
     "movq 64(%[save]), %%rax\n\t"
     "movq %%rcx, 64(%[save])\n\t"

     "movq 16(%[save]), %%rcx\n\t"
     "movq %%rbx, 16(%[save])\n\t"
     "movq %%rcx, %%rbx\n\t"

     "jmpq *%%rax\n"
     /*
       Come here when operation is done.
       Be sure to use the same callee-save register for %[save] here and in
       my_context_spawn(), so we preserve the value correctly at this point.
     */
     "1:\n\t"
     "movq (%[save]), %%rsp\n\t"
     "movq 8(%[save]), %%rbp\n\t"
     /* %rbx is preserved from my_context_spawn() in this case. */
     "movq 24(%[save]), %%r12\n\t"
     "movq 32(%[save]), %%r13\n\t"
     "movq 40(%[save]), %%r14\n\t"
     "movq 48(%[save]), %%r15\n\t"
     "xorl %[ret], %[ret]\n\t"
     "jmp 3f\n"
     /* Come here when operation is suspended. */
     "2:\n\t"
     "movl $1, %[ret]\n"
     "3:\n"
     : [ret] "=a" (ret)
     : /* Need this in callee-save register to preserve in function call. */
       [save] "b" (&c->save[0])
     : "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11", "memory", "cc"
        );

  return ret;
}

int
my_context_yield(struct my_context *c)
{
  uint64_t *save= &c->save[0];
  __asm__ __volatile__
    (
     "movq (%[save]), %%rax\n\t"
     "movq %%rsp, (%[save])\n\t"
     "movq %%rax, %%rsp\n\t"
     "movq 8(%[save]), %%rax\n\t"
     "movq %%rbp, 8(%[save])\n\t"
     "movq %%rax, %%rbp\n\t"
     "movq 16(%[save]), %%rax\n\t"
     "movq %%rbx, 16(%[save])\n\t"
     "movq %%rax, %%rbx\n\t"
     "movq 24(%[save]), %%rax\n\t"
     "movq %%r12, 24(%[save])\n\t"
     "movq %%rax, %%r12\n\t"
     "movq 32(%[save]), %%rax\n\t"
     "movq %%r13, 32(%[save])\n\t"
     "movq %%rax, %%r13\n\t"
     "movq 40(%[save]), %%rax\n\t"
     "movq %%r14, 40(%[save])\n\t"
     "movq %%rax, %%r14\n\t"
     "movq 48(%[save]), %%rax\n\t"
     "movq %%r15, 48(%[save])\n\t"
     "movq %%rax, %%r15\n\t"
     "movq 64(%[save]), %%rax\n\t"
     "leaq 1f(%%rip), %%rcx\n\t"
     "movq %%rcx, 64(%[save])\n\t"

     "jmpq *%%rax\n"

     "1:\n"
     : [save] "+D" (save)
     :
     : "rax", "rcx", "rdx", "rsi", "r8", "r9", "r10", "r11", "memory", "cc"
     );
  return 0;
}

int
my_context_init(struct my_context *c, size_t stack_size)
{
  memset(c, 0, sizeof(*c));

  if (!(c->stack_bot= malloc(stack_size)))
    return -1;                                  /* Out of memory */
  /*
    The x86_64 ABI specifies 16-byte stack alignment.
    Also put two zero words at the top of the stack.
  */
  c->stack_top= (void *)
    (( ((intptr)c->stack_bot + stack_size) & ~(intptr)0xf) - 16);
  memset(c->stack_top, 0, 16);

#ifdef HAVE_VALGRIND
  c->valgrind_stack_id=
    VALGRIND_STACK_REGISTER(c->stack_bot, c->stack_top);
#endif
  return 0;
}

void
my_context_destroy(struct my_context *c)
{
  if (c->stack_bot)
  {
    free(c->stack_bot);
#ifdef HAVE_VALGRIND
    VALGRIND_STACK_DEREGISTER(c->valgrind_stack_id);
#endif
  }
}

#endif  /* MY_CONTEXT_USE_X86_64_GCC_ASM */


#ifdef MY_CONTEXT_USE_I386_GCC_ASM
/*
  GCC-i386 implementation of my_context.

  This is slightly optimized in the common case where we never yield
  (eg. fetch next row and it is already fully received in buffer). In this
  case we do not need to restore registers at return (though we still need to
  save them as we cannot know if we will yield or not in advance).
*/

/*
  Layout of saved registers etc.
  Since this is accessed through gcc inline assembler, it is simpler to just
  use numbers than to try to define nice constants or structs.

   0    0   %esp
   1    4   %ebp
   2    8   %ebx
   3   12   %esi
   4   16   %edi
   5   20   %eip for done
   6   24   %eip for yield/continue
*/

int
my_context_spawn(struct my_context *c, void (*f)(void *), void *d)
{
  int ret;

  /*
    There are 4 callee-save registers we need to save and restore when
    suspending and continuing, plus stack pointer %esp and instruction pointer
    %eip.

    However, if we never suspend, the user-supplied function will in any case
    restore the 4 callee-save registers, so we can avoid restoring them in
    this case.
  */
  __asm__ __volatile__
    (
     "movl %%esp, (%[save])\n\t"
     "movl %[stack], %%esp\n\t"
#if defined(__GCC_HAVE_DWARF2_CFI_ASM) || (defined(__clang__) && __clang_major__ < 13)
     /*
       This emits a DWARF DW_CFA_undefined directive to make the return address
       undefined. This indicates that this is the top of the stack frame, and
       helps tools that use DWARF stack unwinding to obtain stack traces.
       (I use numeric constant to avoid a dependency on libdwarf includes).
     */
     ".cfi_escape 0x07, 8\n\t"
#endif
     /* Push the parameter on the stack. */
     "pushl %[d]\n\t"
     "movl %%ebp, 4(%[save])\n\t"
     "movl %%ebx, 8(%[save])\n\t"
     "movl %%esi, 12(%[save])\n\t"
     "movl %%edi, 16(%[save])\n\t"
     /* Get label addresses in -fPIC-compatible way (no pc-relative on 32bit) */
     "call 1f\n"
     "1:\n\t"
     "popl %%eax\n\t"
     "addl $(2f-1b), %%eax\n\t"
     "movl %%eax, 20(%[save])\n\t"
     "addl $(3f-2f), %%eax\n\t"
     "movl %%eax, 24(%[save])\n\t"
     "call *%[f]\n\t"
     "jmp *20(%[save])\n"
     /*
       Come here when operation is done.
       We do not need to restore callee-save registers, as the called function
       will do this for us if needed.
     */
     "2:\n\t"
     "movl (%[save]), %%esp\n\t"
     "xorl %[ret], %[ret]\n\t"
     "jmp 4f\n"
     /* Come here when operation was suspended. */
     "3:\n\t"
     "movl $1, %[ret]\n"
     "4:\n"
     : [ret] "=a" (ret),
       [f] "+c" (f),
       [d] "+d" (d)
     : [stack] "a" (c->stack_top),
       /* Need this in callee-save register to preserve across function call. */
       [save] "D" (&c->save[0])
     : "memory", "cc"
  );

  return ret;
}

int
my_context_continue(struct my_context *c)
{
  int ret;

  __asm__ __volatile__
    (
     "movl (%[save]), %%eax\n\t"
     "movl %%esp, (%[save])\n\t"
     "movl %%eax, %%esp\n\t"
     "movl 4(%[save]), %%eax\n\t"
     "movl %%ebp, 4(%[save])\n\t"
     "movl %%eax, %%ebp\n\t"
     "movl 8(%[save]), %%eax\n\t"
     "movl %%ebx, 8(%[save])\n\t"
     "movl %%eax, %%ebx\n\t"
     "movl 12(%[save]), %%eax\n\t"
     "movl %%esi, 12(%[save])\n\t"
     "movl %%eax, %%esi\n\t"

     "movl 24(%[save]), %%eax\n\t"
     "call 1f\n"
     "1:\n\t"
     "popl %%ecx\n\t"
     "addl $(2f-1b), %%ecx\n\t"
     "movl %%ecx, 20(%[save])\n\t"
     "addl $(3f-2f), %%ecx\n\t"
     "movl %%ecx, 24(%[save])\n\t"

     /* Must restore %edi last as it is also our %[save] register. */
     "movl 16(%[save]), %%ecx\n\t"
     "movl %%edi, 16(%[save])\n\t"
     "movl %%ecx, %%edi\n\t"

     "jmp *%%eax\n"
     /*
       Come here when operation is done.
       Be sure to use the same callee-save register for %[save] here and in
       my_context_spawn(), so we preserve the value correctly at this point.
     */
     "2:\n\t"
     "movl (%[save]), %%esp\n\t"
     "movl 4(%[save]), %%ebp\n\t"
     "movl 8(%[save]), %%ebx\n\t"
     "movl 12(%[save]), %%esi\n\t"
     "movl 16(%[save]), %%edi\n\t"
     "xorl %[ret], %[ret]\n\t"
     "jmp 4f\n"
     /* Come here when operation is suspended. */
     "3:\n\t"
     "movl $1, %[ret]\n"
     "4:\n"
     : [ret] "=a" (ret)
     : /* Need this in callee-save register to preserve in function call. */
       [save] "D" (&c->save[0])
     : "ecx", "edx", "memory", "cc"
        );

  return ret;
}

int
my_context_yield(struct my_context *c)
{
  uint64_t *save= &c->save[0];
  __asm__ __volatile__
    (
     "movl (%[save]), %%eax\n\t"
     "movl %%esp, (%[save])\n\t"
     "movl %%eax, %%esp\n\t"
     "movl 4(%[save]), %%eax\n\t"
     "movl %%ebp, 4(%[save])\n\t"
     "movl %%eax, %%ebp\n\t"
     "movl 8(%[save]), %%eax\n\t"
     "movl %%ebx, 8(%[save])\n\t"
     "movl %%eax, %%ebx\n\t"
     "movl 12(%[save]), %%eax\n\t"
     "movl %%esi, 12(%[save])\n\t"
     "movl %%eax, %%esi\n\t"
     "movl 16(%[save]), %%eax\n\t"
     "movl %%edi, 16(%[save])\n\t"
     "movl %%eax, %%edi\n\t"

     "movl 24(%[save]), %%eax\n\t"
     "call 1f\n"
     "1:\n\t"
     "popl %%ecx\n\t"
     "addl $(2f-1b), %%ecx\n\t"
     "movl %%ecx, 24(%[save])\n\t"

     "jmp *%%eax\n"

     "2:\n"
     : [save] "+d" (save)
     :
     : "eax", "ecx", "memory", "cc"
     );
  return 0;
}

int
my_context_init(struct my_context *c, size_t stack_size)
{
  memset(c, 0, sizeof(*c));
  if (!(c->stack_bot= malloc(stack_size)))
    return -1;                                  /* Out of memory */
  c->stack_top= (void *)
    (( ((intptr)c->stack_bot + stack_size) & ~(intptr)0xf) - 16);
  memset(c->stack_top, 0, 16);

#ifdef HAVE_VALGRIND
  c->valgrind_stack_id=
    VALGRIND_STACK_REGISTER(c->stack_bot, c->stack_top);
#endif
  return 0;
}

void
my_context_destroy(struct my_context *c)
{
  if (c->stack_bot)
  {
    free(c->stack_bot);
#ifdef HAVE_VALGRIND
    VALGRIND_STACK_DEREGISTER(c->valgrind_stack_id);
#endif
  }
}

#endif  /* MY_CONTEXT_USE_I386_GCC_ASM */


#ifdef MY_CONTEXT_USE_AARCH64_GCC_ASM

/* According to ARM, the instruction BTI [J][C] is a NOP in ARMv8
implementations that do not support it. However, the built-in assembler
in clang 9 through 11 would refuse to emit the instruction unless a
late enough ARMv8.x-A is specified.

On GCC, we cannot easily detect which assembler is going to be invoked
to translate the symbolic assembler code to binary. Hence, we will always
use the numeric encoding in order to be compatible with old assemblers. */

#if defined __clang_major__ && __clang_major__ >= 12
# define BTI_J_STR "bti j"
#else
# define BTI_J_STR ".inst 0xd503249f"
#endif
/*
  GCC-aarch64 (arm64) implementation of my_context.

  This is slightly optimized in the common case where we never yield
  (eg. fetch next row and it is already fully received in buffer). In this
  case we do not need to restore registers at return (though we still need to
  save them as we cannot know if we will yield or not in advance).
*/

/*
  Layout of saved registers etc.
  Since this is accessed through gcc inline assembler, it is simpler to just
  use numbers than to try to define nice constants or structs.

   0    0   x19
   1    8   x20
   2   16   x21
   ...
   9   72   x28
  10   80   x29  (frame pointer)
  11   88   sp
  12   96   d8
  13  104   d9
   ...
  19  152   d15
  20  160   pc for done
  21  168   pc for yield/continue
*/

int
my_context_spawn(struct my_context *c, void (*f)(void *), void *d)
{
  register int ret asm("w0");
  register void (*f_reg)(void *) asm("x1") = f;
  register void *d_reg asm("x2") = d;
  register void *stack asm("x13") = c->stack_top;
  /* Need this in callee-save register to preserve in function call. */
  register const uint64_t *save asm("x19") = &c->save[0];

  /*
    There are a total of 20 callee-save registers (including frame pointer and
    link register) we need to save and restore when suspending and continuing,
    plus stack pointer sp and program counter pc.

    However, if we never suspend, the user-supplied function will in any case
    restore the callee-save registers, so we can avoid restoring them in this
    case.
  */
  __asm__ __volatile__
    (
     "mov x10, sp\n\t"
     "mov sp, %[stack]\n\t"
#if defined(__GCC_HAVE_DWARF2_CFI_ASM) || (defined(__clang__) && __clang_major__ < 13)
     /*
       This emits a DWARF DW_CFA_undefined directive to make the return address
       (UNW_AARCH64_X30) undefined. This indicates that this is the top of the
       stack frame, and helps tools that use DWARF stack unwinding to obtain
       stack traces. (I use numeric constant to avoid a dependency on libdwarf
       includes).
     */
     ".cfi_escape 0x07, 30\n\t"
#endif
     "stp x19, x20, [%[save], #0]\n\t"
     "stp x21, x22, [%[save], #16]\n\t"
     "stp x23, x24, [%[save], #32]\n\t"
     "stp x25, x26, [%[save], #48]\n\t"
     "stp x27, x28, [%[save], #64]\n\t"
     "stp x29, x10, [%[save], #80]\n\t"
     "stp d8, d9,   [%[save], #96]\n\t"
     "stp d10, d11, [%[save], #112]\n\t"
     "stp d12, d13, [%[save], #128]\n\t"
     "stp d14, d15, [%[save], #144]\n\t"
     "adr x10, 1f\n\t"
     "adr x11, 2f\n\t"
     "stp x10, x11, [%[save], #160]\n\t"

     /* Need this in x0 to follow calling convention. */
     "mov x0, %[d]\n\t"
     "blr %[f]\n\t"
     "ldr x11, [%[save], #160]\n\t"
     "br x11\n"
     /*
       Come here when operation is done.
       We do not need to restore callee-save registers, as the called function
       will do this for us if needed.
     */
     "1:\n\t"
     BTI_J_STR "\n\t"
     "ldr x10, [%[save], #88]\n\t"
     "mov sp, x10\n\t"
     "mov %w[ret], #0\n\t"
     "b 3f\n"
     /* Come here when operation was suspended. */
     "2:\n\t"
     BTI_J_STR "\n\t"
     "mov %w[ret], #1\n"
     "3:\n"
     : [ret] "=r" (ret),
       [f] "+r" (f_reg),
       [d] "+r" (d_reg),
       [stack] "+r" (stack)
     : [save] "r" (save)
     : "x3", "x4", "x5", "x6", "x7",
       "x9", "x10", "x11", "x14", "x15",
#if defined(__linux__) && !defined(__ANDROID__)
       "x18",
#endif
       "x30",
       "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7",
       "v16", "v17", "v18", "v19", "v20", "v21", "v22", "v23",
       "v24", "v25", "v26", "v27", "v28", "v29", "v30", "v31",
       "memory", "cc"
  );

  return ret;
}

int
my_context_continue(struct my_context *c)
{
  register int ret asm("w0");
  /* Need this in callee-save register to preserve in function call. */
  register const uint64_t *save asm("x19") = &c->save[0];

  __asm__ __volatile__
    (
     "ldp x13, x11, [%[save], #0]\n\t"
     "stp x19, x20, [%[save], #0]\n\t"
     /* x19 is %[save], delay restoring it until %[save] is no longer needed. */
     "mov x20, x11\n\t"

     "ldp x10, x11, [%[save], #16]\n\t"
     "stp x21, x22, [%[save], #16]\n\t"
     "mov x21, x10\n\t"
     "mov x22, x11\n\t"

     "ldp x10, x11, [%[save], #32]\n\t"
     "stp x23, x24, [%[save], #32]\n\t"
     "mov x23, x10\n\t"
     "mov x24, x11\n\t"

     "ldp x10, x11, [%[save], #48]\n\t"
     "stp x25, x26, [%[save], #48]\n\t"
     "mov x25, x10\n\t"
     "mov x26, x11\n\t"

     "ldp x10, x11, [%[save], #64]\n\t"
     "stp x27, x28, [%[save], #64]\n\t"
     "mov x27, x10\n\t"
     "mov x28, x11\n\t"

     "ldp x10, x11, [%[save], #80]\n\t"
     "mov x14, sp\n\t"
     "stp x29, x14, [%[save], #80]\n\t"
     "mov x29, x10\n\t"
     "mov sp, x11\n\t"

     "ldp d0, d1, [%[save], #96]\n\t"
     "stp d8, d9, [%[save], #96]\n\t"
     "fmov d8, d0\n\t"
     "fmov d9, d1\n\t"

     "ldp d0, d1, [%[save], #112]\n\t"
     "stp d10, d11, [%[save], #112]\n\t"
     "fmov d10, d0\n\t"
     "fmov d11, d1\n\t"

     "ldp d0, d1, [%[save], #128]\n\t"
     "stp d12, d13, [%[save], #128]\n\t"
     "fmov d12, d0\n\t"
     "fmov d13, d1\n\t"

     "ldp d0, d1, [%[save], #144]\n\t"
     "stp d14, d15, [%[save], #144]\n\t"
     "fmov d14, d0\n\t"
     "fmov d15, d1\n\t"

     "adr x10, 1f\n\t"
     "adr x11, 2f\n\t"
     "ldr x15, [%[save], #168]\n\t"
     "stp x10, x11, [%[save], #160]\n\t"
     "mov x19, x13\n\t"
     "br x15\n"
     /*
       Come here when operation is done.
       Be sure to use the same callee-save register for %[save] here and in
       my_context_spawn(), so we preserve the value correctly at this point.
     */
     "1:\n\t"
     BTI_J_STR "\n\t"
     /* x19 (aka %[save]) is preserved from my_context_spawn() in this case. */
     "ldr x20, [%[save], #8]\n\t"
     "ldp x21, x22, [%[save], #16]\n\t"
     "ldp x23, x24, [%[save], #32]\n\t"
     "ldp x25, x26, [%[save], #48]\n\t"
     "ldp x27, x28, [%[save], #64]\n\t"
     "ldp x29, x10, [%[save], #80]\n\t"
     "mov sp, x10\n\t"
     "ldp d8, d9, [%[save], #96]\n\t"
     "ldp d10, d11, [%[save], #112]\n\t"
     "ldp d12, d13, [%[save], #128]\n\t"
     "ldp d14, d15, [%[save], #144]\n\t"
     "mov %w[ret], #0\n\t"
     "b 3f\n"
     /* Come here when operation is suspended. */
     "2:\n\t"
     BTI_J_STR "\n\t"
     "mov %w[ret], #1\n"
     "3:\n"
     : [ret] "=r" (ret)
     : [save] "r" (save)
     : "x1", "x2", "x3", "x4", "x5", "x6", "x7",
       "x9", "x10", "x11", "x12", "x13", "x14", "x15",
#if defined(__linux__) && !defined(__ANDROID__)
       "x18",
#endif
       "x30",
       "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7",
       "v16", "v17", "v18", "v19", "v20", "v21", "v22", "v23",
       "v24", "v25", "v26", "v27", "v28", "v29", "v30", "v31",
       "memory", "cc"
        );

  return ret;
}

int
my_context_yield(struct my_context *c)
{
  register const uint64_t *save asm("x19") = &c->save[0];
  __asm__ __volatile__
    (
     "ldp x13, x11, [%[save], #0]\n\t"
     "stp x19, x20, [%[save], #0]\n\t"
     /* x19 is %[save], delay restoring it until %[save] is no longer needed. */
     "mov x20, x11\n\t"

     "ldp x10, x11, [%[save], #16]\n\t"
     "stp x21, x22, [%[save], #16]\n\t"
     "mov x21, x10\n\t"
     "mov x22, x11\n\t"

     "ldp x10, x11, [%[save], #32]\n\t"
     "stp x23, x24, [%[save], #32]\n\t"
     "mov x23, x10\n\t"
     "mov x24, x11\n\t"

     "ldp x10, x11, [%[save], #48]\n\t"
     "stp x25, x26, [%[save], #48]\n\t"
     "mov x25, x10\n\t"
     "mov x26, x11\n\t"

     "ldp x10, x11, [%[save], #64]\n\t"
     "stp x27, x28, [%[save], #64]\n\t"
     "mov x27, x10\n\t"
     "mov x28, x11\n\t"

     "ldp x10, x11, [%[save], #80]\n\t"
     "mov x14, sp\n\t"
     "stp x29, x14, [%[save], #80]\n\t"
     "mov x29, x10\n\t"
     "mov sp, x11\n\t"

     "ldp d0, d1, [%[save], #96]\n\t"
     "stp d8, d9, [%[save], #96]\n\t"
     "fmov d8, d0\n\t"
     "fmov d9, d1\n\t"

     "ldp d0, d1, [%[save], #112]\n\t"
     "stp d10, d11, [%[save], #112]\n\t"
     "fmov d10, d0\n\t"
     "fmov d11, d1\n\t"

     "ldp d0, d1, [%[save], #128]\n\t"
     "stp d12, d13, [%[save], #128]\n\t"
     "fmov d12, d0\n\t"
     "fmov d13, d1\n\t"

     "ldp d0, d1, [%[save], #144]\n\t"
     "stp d14, d15, [%[save], #144]\n\t"
     "fmov d14, d0\n\t"
     "fmov d15, d1\n\t"

     "ldr x11, [%[save], #168]\n\t"
     "adr x10, 1f\n\t"
     "str x10, [%[save], #168]\n\t"
     "mov x19, x13\n\t"
     "br x11\n"

     "1:\n"
     BTI_J_STR "\n\t"
     :
     : [save] "r" (save)
     : "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7",
       "x9", "x10", "x11", "x12", "x13", "x14", "x15",
#if defined(__linux__) && !defined(__ANDROID__)
       "x18",
#endif
       "x30",
       "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7",
       "v16", "v17", "v18", "v19", "v20", "v21", "v22", "v23",
       "v24", "v25", "v26", "v27", "v28", "v29", "v30", "v31",
       "memory", "cc"
     );
  return 0;
}

int
my_context_init(struct my_context *c, size_t stack_size)
{
  memset(c, 0, sizeof(*c));

  if (!(c->stack_bot= malloc(stack_size)))
    return -1;                                  /* Out of memory */
  /*
    Align stack to 16-byte boundary.
    Also put two zero words at the top of the stack.
  */
  c->stack_top= (void *)
    (( ((intptr)c->stack_bot + stack_size) & ~(intptr)0xf) - 16);
  memset(c->stack_top, 0, 16);

#ifdef HAVE_VALGRIND
  c->valgrind_stack_id=
    VALGRIND_STACK_REGISTER(c->stack_bot, c->stack_top);
#endif
  return 0;
}

void
my_context_destroy(struct my_context *c)
{
  if (c->stack_bot)
  {
    free(c->stack_bot);
#ifdef HAVE_VALGRIND
    VALGRIND_STACK_DEREGISTER(c->valgrind_stack_id);
#endif
  }
}

#endif  /* MY_CONTEXT_USE_AARCH64_GCC_ASM */


#ifdef MY_CONTEXT_USE_WIN32_FIBERS
int
my_context_yield(struct my_context *c)
{
  c->return_value= 1;
  SwitchToFiber(c->app_fiber);
  return 0;
}


static void WINAPI
my_context_trampoline(void *p)
{
  struct my_context *c= (struct my_context *)p;
  /*
    Reuse the Fiber by looping infinitely, each time we are scheduled we
    spawn the appropriate function and switch back when it is done.

    This way we avoid the overhead of CreateFiber() for every asynchronous
    operation.
  */
  for(;;)
  {
    (*(c->user_func))(c->user_arg);
    c->return_value= 0;
    SwitchToFiber(c->app_fiber);
  }
}

int
my_context_init(struct my_context *c, size_t stack_size)
{
  memset(c, 0, sizeof(*c));
  c->lib_fiber= CreateFiber(stack_size, my_context_trampoline, c);
  if (c->lib_fiber)
    return 0;
  return -1;
}

void
my_context_destroy(struct my_context *c)
{
  if (c->lib_fiber)
  {
    DeleteFiber(c->lib_fiber);
    c->lib_fiber= NULL;
  }
}

int
my_context_spawn(struct my_context *c, void (*f)(void *), void *d)
{
  c->user_func= f;
  c->user_arg= d;
  return my_context_continue(c);
}

int
my_context_continue(struct my_context *c)
{
  void *current_fiber=  IsThreadAFiber() ? GetCurrentFiber() : ConvertThreadToFiber(c);
  c->app_fiber= current_fiber;
  SwitchToFiber(c->lib_fiber);
  return c->return_value;
}

#endif  /* MY_CONTEXT_USE_WIN32_FIBERS */

#ifdef MY_CONTEXT_DISABLE
int
my_context_continue(struct my_context *c)
{
  return -1;
}


int
my_context_spawn(struct my_context *c, void (*f)(void *), void *d)
{
  return -1;
}


int
my_context_yield(struct my_context *c)
{
  return -1;
}

int
my_context_init(struct my_context *c, size_t stack_size)
{
  return -1;                                  /* Out of memory */
}

void
my_context_destroy(struct my_context *c)
{
}

#endif
