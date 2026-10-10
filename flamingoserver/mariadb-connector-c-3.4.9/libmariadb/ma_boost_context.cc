#include "ma_config.h"
#include "mysql.h"
#include "ma_context.h"

#ifdef MY_CONTEXT_USE_BOOST_CONTEXT

#include <boost/fiber/context.hpp>

namespace ctx=boost::context;

struct my_context_intern {
  ctx::fiber parent, coro;

  void *stack_top(const my_context *c) {
    return (unsigned char *)(c->stack) + c->stack_size;
  }

  /* A StackAlloc for ctx::fiber that reuses our stack. */
  struct my_stack_alloc {
    typedef ctx::stack_traits traits_type;
    my_context *c;
    my_stack_alloc(my_context *c_arg) : c(c_arg) { };
    ctx::stack_context allocate() {
      ctx::stack_context sctx;
      sctx.size= c->stack_size;
      sctx.sp= ((my_context_intern *)c->internal_context)->stack_top(c);
#if defined(BOOST_USE_VALGRIND) && defined(HAVE_VALGRIND)
      sctx.valgrind_stack_id= c->valgrind_stack_id;
#endif
      return sctx;
    }
    void deallocate(ctx::stack_context & sctx) {
      /* Empty, we will re-use the stack. */
    }
  };
};


extern "C"
int
my_context_spawn(struct my_context *c, void (*f)(void *), void *d)
{
  my_context_intern *ci= (my_context_intern *)c->internal_context;
  ci->coro= ctx::fiber(std::allocator_arg, my_context_intern::my_stack_alloc(c),
                       [c, f, d](ctx::fiber && parent) {
      my_context_intern *ci= (my_context_intern *)c->internal_context;
      ci->parent= std::move(parent);
      (*f)(d);
      c->active= 0;
      return std::move(ci->parent);
    });
  c->active= 1;
  ci->coro= std::move(ci->coro).resume();
  return c->active;
}


extern "C"
int
my_context_continue(struct my_context *c)
{
  if (!c->active)
    return 0;
  my_context_intern *ci= (my_context_intern *)c->internal_context;
  ci->coro= std::move(ci->coro).resume();
  return c->active;
}


extern "C"
int
my_context_yield(struct my_context *c)
{
  if (!c->active)
    return -1;
  my_context_intern *ci= (my_context_intern *)c->internal_context;
  ci->parent= std::move(ci->parent).resume();
  return 0;
}


extern "C"
int
my_context_init(struct my_context *c, size_t stack_size)
{
  memset(c, 0, sizeof(*c));
  if (!(c->stack= malloc(stack_size)))
    return -1;                                  /* Out of memory */
  if (!(c->internal_context= new my_context_intern))
  {
    free(c->stack);
    return -1;
  }
  c->stack_size= stack_size;
#ifdef HAVE_VALGRIND
  c->valgrind_stack_id=
    VALGRIND_STACK_REGISTER(c->stack, ((unsigned char *)(c->stack))+stack_size);
#endif
  return 0;
}


extern "C"
void
my_context_destroy(struct my_context *c)
{
  delete (my_context_intern *)c->internal_context;
  if (c->stack)
  {
#ifdef HAVE_VALGRIND
    VALGRIND_STACK_DEREGISTER(c->valgrind_stack_id);
#endif
    free(c->stack);
  }
}

#endif /* MY_CONTEXT_USE_BOOST_CONTEXT */
