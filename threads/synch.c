/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
*/

#include "threads/synch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "lib/kernel/list.h"
#include "threads/malloc.h"

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
     decrement it.

   - up or "V": increment the value (and wake up one waiting
     thread, if any). */
void sema_init(struct semaphore *sema, unsigned value)
{
  ASSERT(sema != NULL);

  sema->value = value;
  list_init(&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. */
void sema_down(struct semaphore *sema)
{
  enum intr_level old_level;

  ASSERT(sema != NULL);
  ASSERT(!intr_context());

  old_level = intr_disable();
  struct thread *cur = thread_current();
  while (sema->value == 0)
  {
    list_push_ordered(&sema->waiters, &thread_current()->elem, thread_current()->priority);
    //list_push_back(&sema->waiters, &thread_current()->elem);
    thread_block(false);
  }
  sema->value--;
  intr_set_level(old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool sema_try_down(struct semaphore *sema)
{
  enum intr_level old_level;
  bool success;

  ASSERT(sema != NULL);

  old_level = intr_disable();
  if (sema->value > 0)
  {
    sema->value--;
    success = true;
  }
  else
    success = false;
  intr_set_level(old_level);

  return success;
}

bool sema_cmp(const struct list_elem *a, const struct list_elem *b, void *null) //the comparsion function
{
  return list_entry(a, struct thread, elem)->priority > list_entry(b, struct thread, elem)->priority;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void sema_up(struct semaphore *sema)
{
  enum intr_level old_level;

  ASSERT(sema != NULL);

  old_level = intr_disable();
  if (!list_empty(&sema->waiters)) //while
  {
    list_sort(&sema->waiters, sema_cmp, NULL); //make the waiting list into ordered
    struct thread *thread = list_entry(list_pop_front(&sema->waiters), struct thread, elem);

    thread_unblock(thread);
  }
  sema->value++;
  thread_yield();
  intr_set_level(old_level);
}

static void sema_test_helper(void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void sema_self_test(void)
{
  struct semaphore sema[2];
  int i;

  printf("Testing semaphores...");
  sema_init(&sema[0], 0);
  sema_init(&sema[1], 0);
  thread_create("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
  for (i = 0; i < 10; i++)
  {
    sema_up(&sema[0]);
    sema_down(&sema[1]);
  }
  printf("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper(void *sema_)
{
  struct semaphore *sema = sema_;
  int i;

  for (i = 0; i < 10; i++)
  {
    sema_down(&sema[0]);
    sema_up(&sema[1]);
  }
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void lock_init(struct lock *lock)
{
  ASSERT(lock != NULL);

  lock->holder = NULL;
  sema_init(&lock->semaphore, 1);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void lock_acquire(struct lock *lock)
{
  ASSERT(lock != NULL);
  ASSERT(!intr_context());
  ASSERT(!lock_held_by_current_thread(lock));
  enum intr_level old_level = intr_disable();
  if (lock->holder != NULL)
    donate_priority(lock);
  sema_down(&lock->semaphore);
  intr_set_level(old_level);
  lock->holder = thread_current();
}

/* 
  捐赠优先级入口
  将锁分为直接锁和递归锁。在donation_log的bool值上标明
 */
void donate_priority(struct lock *lock)
{
  enum intr_level old_level = intr_disable();

  struct thread *cur = thread_current();
  struct thread *holder = lock->holder;
  int cur_should_be;
  // cur->priority < holder->priority
  if (cur->priority > holder->priority)
  {
    struct donation_log *log = NULL;
    struct donation_log *new_log;
    //创建一个捐赠记录并把它接到列表尾部
    do
    {
      //当找到递归捐赠记录时，需要在生成新记录前改变一些设置
      //当需要递归捐赠时，相当于是最高优先级的线程向捐赠链中的每一个环节都请求一次锁。
      if (log != NULL) //log == NULL意味着是非递归捐赠
      {
        holder = log->receiver;
        lock = log->mutex;
        log->is_nest_donation = true;
      }
      new_log = (struct donation_log *)malloc(sizeof(struct donation_log));
      //这里可不能随便free。。。free掉了整个donation_log就从内存中消失了
      new_log->donator = cur;
      new_log->donator_pri = cur->priority;
      new_log->mutex = lock;
      new_log->receiver = holder;
      new_log->receiver_pri = holder->priority;
      new_log->is_nest_donation = (log == NULL ? false : true); //若是递归捐赠，设置提示位
      cur_should_be = holder->priority;
      holder->priority = cur->priority;
      list_insert_ordered(&donation_list, &new_log->don_elem, less, NULL);

      //交换优先级后需要将ready-list里高优先级的线程提前
      if (holder->status == THREAD_READY)
        thread_promote(holder);
    } while ((log = find_donation_log(new_log->receiver)) != NULL);
    cur->priority = cur_should_be;
  }

  intr_set_level(old_level);
}

struct donation_log *find_donation_log(struct thread *my_thread)
{
  struct list_elem *e;
  for (e = list_begin(&donation_list); e != list_end(&donation_list); e = list_next(e))
  {
    struct donation_log *log = list_entry(e, struct donation_log, don_elem);
    if (log->donator == my_thread)
      return log;
  }
  return NULL;
}

/* 用于将donation_log按序插入到donation_list中的比较函数 */
bool less(struct list_elem *a, struct list_elem *b, void *aux)
{
  struct donation_log *log_a = list_entry(a, struct donation_log, don_elem);
  struct donation_log *log_b = list_entry(b, struct donation_log, don_elem);
  if (log_a->donator_pri <= log_b->donator_pri)
    return false;
  else
    return true;
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool lock_try_acquire(struct lock *lock)
{
  bool success;

  ASSERT(lock != NULL);
  ASSERT(!lock_held_by_current_thread(lock));

  success = sema_try_down(&lock->semaphore);
  if (success)
    lock->holder = thread_current();
  return success;
}

/* Releases LOCK, which must be owned by the current thread.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void lock_release(struct lock *lock)
{
  ASSERT(lock != NULL);
  ASSERT(lock_held_by_current_thread(lock));

  enum intr_level old_level = intr_disable();

  refund_priority(lock);
  lock->holder = NULL;
  sema_up(&lock->semaphore);
  free_donations();
  intr_set_level(old_level);
}

/* 
  释放锁时归还优先级,释放完毕后将所有线程按优先级重排，
  直接锁和递归锁的归还优先级逻辑不一样。
 */
void refund_priority(struct lock *lock)
{
  enum intr_level old_level = intr_disable();
  struct thread *cur = thread_current();
  struct donation_log *log;

  struct list_elem *e, *f;
  bool exist_other_log = false;
  bool cur_pri_should_change;
  //由于free的过程中会thread_yield()，我们不能在free之前修改当前线程的优先级。用这个记录是否应该修改当前线程优先级。
  int target_pri;
  //用这个记录应该将当前线程的优先级调整为哪个数字

  /* 分两种情况来思考：1.释放锁之前发生了优先级下调 ；2.释放锁之前未发生优先级下调 */
  for (e = list_begin(&donation_list); e != list_end(&donation_list); e = list_next(e))
  {
    log = list_entry(e, struct donation_log, don_elem);
    if (log->receiver == cur && log->mutex == lock)
    {
      struct thread *donator = log->donator;
      if (log->donator_pri == cur->priority)
      {
        cur->priority = log->receiver_pri;
        //如果是递归锁，那么此时不能动捐赠者的优先级，仅仅下调当前线程的优先级
        if (!log->is_nest_donation)
          donator->priority = log->donator_pri;

        f = list_prev(e);
        list_remove(e);
        e = f;
        list_push_back(&donation2delete, &log->don_elem);
      }
      else if (log->donator_pri < cur->priority)
      {
        if (log->is_nest_donation)
          //证明donator曾参与过递归捐赠！
          cur->priority = log->receiver_pri;
        else
          donator->priority = log->donator_pri;
        f = list_prev(e);
        list_remove(e);
        e = f;
        list_push_back(&donation2delete, &log->don_elem);
      }
    }
    else if (log->receiver == cur && log->mutex != lock)
      exist_other_log = true;
  }
  //此时锁已经放干净了！
  //当不存在与该线程有关的其他捐赠记录，说明优先级应回到real_priority
  if (!exist_other_log)
    cur->priority = cur->real_priority;
  //归还一部分优先级后，real_priority比priority高，说明归还前发生了优先级下调，且此时没有更高优先级的捐赠
  if (cur->real_priority > cur->priority)
    cur->priority = cur->real_priority;

  intr_set_level(old_level);
}

void free_donations()
{
  struct list_elem *e, *f;
  struct donation_log *log;
  for (e = list_begin(&donation2delete); e != list_end(&donation2delete); e = list_next(e))
  {
    log = list_entry(e, struct donation_log, don_elem);
    f = list_prev(e);
    list_remove(e);
    e = f;
    free(log);
  }
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool lock_held_by_current_thread(const struct lock *lock)
{
  ASSERT(lock != NULL);

  return lock->holder == thread_current();
}

/* One semaphore in a list. */
struct semaphore_elem
{
  struct list_elem elem;      /* List element. */
  struct semaphore semaphore; /* This semaphore. */
};

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void cond_init(struct condition *cond)
{
  ASSERT(cond != NULL);

  list_init(&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void cond_wait(struct condition *cond, struct lock *lock)
{
  struct semaphore_elem waiter;

  ASSERT(cond != NULL);
  ASSERT(lock != NULL);
  ASSERT(!intr_context());
  ASSERT(lock_held_by_current_thread(lock));

  sema_init(&waiter.semaphore, 0);
  list_push_ordered(&cond->waiters, &waiter.elem, 0);
  //list_push_back(&cond->waiters, &waiter.elem);
  lock_release(lock);
  sema_down(&waiter.semaphore);
  lock_acquire(lock);
}

bool cond_cmp(const struct list_elem *a, const struct list_elem *b, void *null)
{
  return list_entry((list_front(&list_entry(a, struct semaphore_elem, elem)->semaphore.waiters)), struct thread, elem)->priority > list_entry((list_front(&list_entry(b, struct semaphore_elem, elem)->semaphore.waiters)), struct thread, elem)->priority;
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */

void cond_signal(struct condition *cond, struct lock *lock UNUSED)
{
  ASSERT(cond != NULL);
  ASSERT(lock != NULL);
  ASSERT(!intr_context());
  ASSERT(lock_held_by_current_thread(lock));

  if (!list_empty(&cond->waiters))
  {
    list_sort(&cond->waiters, cond_cmp, NULL); //make the list into ordered before wake
    sema_up(&list_entry(list_pop_front(&cond->waiters),
                        struct semaphore_elem, elem)
                 ->semaphore);
  }
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void cond_broadcast(struct condition *cond, struct lock *lock)
{
  ASSERT(cond != NULL);
  ASSERT(lock != NULL);

  while (!list_empty(&cond->waiters))
    cond_signal(cond, lock);
}
