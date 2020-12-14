#include "context.h"
#include <assert.h>
#include <sched.h>
#include <stdlib.h>

static __thread context_t* cur;

context_t::context_t()
  : creator(NULL), func(NULL), arg(NULL),
#ifndef USE_UCONTEXT
    mutex(PTHREAD_MUTEX_INITIALIZER),
    cond(PTHREAD_COND_INITIALIZER), flag(0)
#else
    context(new ucontext_t)
#endif
{
}

#ifdef USE_UCONTEXT//使用上下文切换
#ifndef GLIBC_64BIT_PTR_BUG
void context_t::wrapper(context_t* ctx)
{
#else
void context_t::wrapper(unsigned int hi, unsigned int lo)
{
  context_t* ctx = reinterpret_cast<context_t*>(static_cast<unsigned long>(lo) | (static_cast<unsigned long>(hi) << 32));
#endif
  ctx->creator->switch_to();
  ctx->func(ctx->arg);
}
#else//使用锁
void* context_t::wrapper(void* a)//传入pthread的函数
{
  context_t* ctx = static_cast<context_t*>(a);
  cur = ctx;
  ctx->creator->switch_to();

  ctx->func(ctx->arg);
  return NULL;
}
#endif

void context_t::init(void (*f)(void*), void* a)
{
  func = f;
  arg = a;
  creator = current();

#ifdef USE_UCONTEXT//使用上下文切换
  getcontext(context.get());//将当前的执行上下文保存，以便后续恢复上下文
  context->uc_link = creator->context.get();//为当前context执行结束之后要执行的下一个context，若un_link为空，执行完当前context之后退出程序
  context->uc_stack.ss_size = 64*1024;
  context->uc_stack.ss_sp = new void*[context->uc_stack.ss_size/sizeof(void*)];
#ifndef GLIBC_64BIT_PTR_BUG
  makecontext(context.get(), (void(*)(void))&context_t::wrapper, 1, this);//初始化上下文
                                                                          //指定入口函数
									  //入口参数的个数					
#else
  unsigned int hi(reinterpret_cast<unsigned long>(this) >> 32);
  unsigned int lo(reinterpret_cast<unsigned long>(this));
  makecontext(context.get(), (void(*)(void))&context_t::wrapper, 2, hi, lo);//初始化上下文
#endif
  switch_to();
#else              //使用锁
  assert(flag == 0);

  pthread_mutex_lock(&creator->mutex);
  creator->flag = 0;
  if (pthread_create(&thread, NULL, &context_t::wrapper, this) != 0)//创建线程
    abort();
  pthread_detach(thread);
  while (!creator->flag)
    pthread_cond_wait(&creator->cond, &creator->mutex);//令进程等待在条件变量上
  pthread_mutex_unlock(&creator->mutex);
#endif
}

context_t::~context_t()
{
  assert(this != cur);
}

void context_t::switch_to()
{
  assert(this != cur);
#ifdef USE_UCONTEXT //使用上下文切换
  context_t* prev = cur;
  cur = this;
  if (swapcontext(prev->context.get(), context.get()) != 0)//保存当前的上下文，并将上下文切换到新的上下文运行
    abort();
#else               //使用锁       
  cur->flag = 0;
  this->flag = 1;


  pthread_mutex_lock(&this->mutex);
  pthread_cond_signal(&this->cond);//发送一个信号给另外一个处于阻塞等待状态的线程，使其脱离阻塞状态，继续执行，如果没有线程处于阻塞等待状态，函数也会成功返回。
  				   //该函数必须放在lock和unlock之间，因为该函数要根据共享变量的状态来决定是否要等待，而为了不永远等待下去，所以必须在lock/unlock队中
				   //通知等待在条件变量上的消费者
  pthread_mutex_unlock(&this->mutex);
  

  pthread_mutex_lock(&cur->mutex);
  while (!cur->flag)
    pthread_cond_wait(&cur->cond, &cur->mutex);
  pthread_mutex_unlock(&cur->mutex);

#endif
}

context_t* context_t::current()
{
  if (cur == NULL)
  {
    cur = new context_t;
#ifdef USE_UCONTEXT
    getcontext(cur->context.get());//保存上下文
#else
    cur->thread = pthread_self();//当前线程ID  标识符
    cur->flag = 1;
#endif
  }
  return cur;
}
