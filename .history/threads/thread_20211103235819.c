#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "devices/timer.h"
//TODO:增加fp库
#include "devices/fixed-point.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

static struct list sleep_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Stack frame for kernel_thread(). */
struct kernel_thread_frame
{
  void *eip;             /* Return address. */
  thread_func *function; /* Function to call. */
  void *aux;             /* Auxiliary data for function. */
};

/* Statistics. */
static long long idle_ticks;   /* # of timer ticks spent idle. */
static long long kernel_ticks; /* # of timer ticks in kernel threads. */
static long long user_ticks;   /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4          /* # of timer ticks to give each thread. */
static unsigned thread_ticks; /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

//TODO:添加了全局变量load_avg(考虑到涉及浮点数运算，应为fp)
fp load_avg;

static void kernel_thread(thread_func *, void *aux);

static void idle(void *aux UNUSED);
static struct thread *running_thread(void);
static struct thread *next_thread_to_run(void);
static void init_thread(struct thread *, const char *name, int priority);
static bool is_thread(struct thread *) UNUSED;
static void *alloc_frame(struct thread *, size_t size);
static void schedule(void);
void thread_schedule_tail(struct thread *prev);
static tid_t allocate_tid(void);


//创建主线程 暂时不确定load_avg需要在thread_init or start开始 在开始调度比较合理（准备运行的平均线程数
/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void thread_init(void)
{
  ASSERT(intr_get_level() == INTR_OFF);

  lock_init(&tid_lock);
  list_init(&ready_list);
  list_init(&all_list);
  list_init(&sleep_list);
  list_init(&donation_list);
  list_init(&donation2delete);

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread();
  init_thread(initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void thread_start(void)
{
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init(&idle_started, 0);
  thread_create("idle", PRI_MIN, idle, &idle_started);

  /* Start preemptive thread scheduling. */
  intr_enable();

  //TODO：初始化系统平均负载
  load_avg = INT_TO_FP(0);

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down(&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void thread_tick(void)
{
  struct thread *t = thread_current();

  /* Update statistics. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;

  /* Enforce preemption. */
  if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return();
}

/* Prints thread statistics. */
void thread_print_stats(void)
{
  printf("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
         idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t thread_create(const char *name, int priority,
                    thread_func *function, void *aux)
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;
  enum intr_level old_level;

  ASSERT(function != NULL);

  /* Allocate thread. */
  t = palloc_get_page(PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread(t, name, priority);
  tid = t->tid = allocate_tid();

  /* Prepare thread for first run by initializing its stack.
     Do this atomically so intermediate values for the 'stack' 
     member cannot be observed. */
  old_level = intr_disable();

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame(t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = alloc_frame(t, sizeof *ef);
  ef->eip = (void (*)(void))kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame(t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  intr_set_level(old_level);

  /* Add to run queue. */
  thread_unblock(t);
  thread_yield();

  return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void thread_block(bool timer_sleep)
{
  ASSERT(!intr_context());
  ASSERT(intr_get_level() == INTR_OFF);

  struct list_elem *e;

  if (timer_sleep)
  {
    if (list_empty(&sleep_list))
      list_push_front(&sleep_list, &thread_current()->sleepelem);

    else
    {
      int has_inserted = 0;
      for (e = list_begin(&sleep_list); e != list_end(&sleep_list); e = list_next(e))
      {
        struct thread *thread = list_entry(e, struct thread, sleepelem);
        if (thread_current()->sleep_end <= thread->sleep_end)
        {
          list_insert(&thread->sleepelem, &thread_current()->sleepelem);
          has_inserted = 1;
          break;
        }
      }
      if (has_inserted == 0)
        list_push_back(&sleep_list, &thread_current()->sleepelem);
    }
  }

  thread_current()->status = THREAD_BLOCKED;
  schedule();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void thread_unblock(struct thread *t)
{
  enum intr_level old_level;

  ASSERT(is_thread(t));

  old_level = intr_disable();
  ASSERT(t->status == THREAD_BLOCKED);
  list_push_ordered(&ready_list, &t->elem, t->priority);
  //按优先级插入队列
  t->status = THREAD_READY;
  intr_set_level(old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name(void)
{
  return thread_current()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current(void)
{
  struct thread *t = running_thread();

  /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4 kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
  ASSERT(is_thread(t));
  ASSERT(t->status == THREAD_RUNNING);

  return t;
}

/* Returns the running thread's tid. */
tid_t thread_tid(void)
{
  return thread_current()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void thread_exit(void)
{
  ASSERT(!intr_context());

#ifdef USERPROG
  process_exit();
#endif

  /* Remove thread from all threads list, set our status to dying,
     and schedule another process.  That process will destroy us
     when it calls thread_schedule_tail(). */
  intr_disable();
  list_remove(&thread_current()->allelem);
  thread_current()->status = THREAD_DYING;
  schedule();
  NOT_REACHED();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void thread_yield(void)
{
  struct thread *cur = thread_current();
  enum intr_level old_level;

  ASSERT(!intr_context());

  old_level = intr_disable();
  if (cur != idle_thread)
    list_push_ordered(&ready_list, &cur->elem, cur->priority);
  //list_push_back(&ready_list, &cur->elem);
  cur->status = THREAD_READY;
  schedule();
  intr_set_level(old_level);
}

/* Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void thread_foreach(thread_action_func *func, void *aux)
{
  struct list_elem *e;

  ASSERT(intr_get_level() == INTR_OFF);

  for (e = list_begin(&all_list); e != list_end(&all_list);
       e = list_next(e))
  {
    struct thread *t = list_entry(e, struct thread, allelem);
    func(t, aux);
  }
}

/* 
  Sets the current thread's priority to NEW_PRIORITY. 
  TODO：这个函数不能总是生效！！！当new_priority小于当前priority，且线程处于被捐赠状态，需要屏蔽此次优先级修改
*/
void thread_set_priority(int new_priority)
{
  struct thread *cur = thread_current();
  if (new_priority >= cur->priority)
  {
    cur->priority = new_priority;
    cur->real_priority = new_priority;
  }
  else
  {
    if (cur->priority > cur->real_priority)
    { //说明处于被捐赠状态
      cur->real_priority = new_priority;
    }
    else
    { //未处于捐赠状态且优先级下调，需要出让cpu
      cur->priority = new_priority;
      cur->real_priority = new_priority;
      thread_yield();
    }
  }
}

/* Returns the current thread's priority. */
int thread_get_priority(void)
{
  return thread_current()->priority;
}

/* 返回线程的真实优先级 */
int thread_get_real_priority(void)
{
  return thread_current()->real_priority;
}


//TODO:BSD调度器待实现内容
//将当前线程的nice值设置为new_nice并根据新值重新计算线程的优先级（参见B.2 计算优先级）。如果正在运行的线程不再具有最高优先级，则yield。
//原形参（nice UNUSED）删除unused
/* Sets the current thread's nice value to NICE. */
void thread_set_nice(int nice )
{
  
  thread_current()->nice= nice > NICE_MAX ? NICE_MAX : nice;
  thread_current()->nice = nice < NICE_MIN ? NICE_MIN : nice;
  thread_current()->nice = nice;
  //重新计算优先级 待补充
  // update_priority_single(thread_current());
  update_priority_current();
  thread_yield();

}


//更改：返回值
/* Returns the current thread's nice value. */
int thread_get_nice(void)
{
  /* Not yet implemented. */
  // return 0;
  return thread_current()->nice;
}
//todo:填空函数

/* Returns 100 times the system load average. */
int thread_get_load_avg(void)
{
  fp tmpfp = MULTI_FP_INT(load_avg,100);
  return FP_TO_INT_NEAREST(tmpfp);

}

/* Returns 100 times the current thread's recent_cpu value. *///DONE
int thread_get_recent_cpu(void)
{
  /* Not yet implemented. */
  // return 0;
  fp cur_cpu_fp = thread_current()->recent_cpu;
  fp cpu_multi = MULTI_FP_INT(cur_cpu_fp,100);
  return FP_TO_INT_NEAREST(cpu_multi); 
}



/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle(void *idle_started_ UNUSED)
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current();
  sema_up(idle_started);

  for (;;)
  {
    /* Let someone else run. */
    intr_disable();
    thread_block(false);

    /* Re-enable interrupts and wait for the next one.

         The `sti' instruction disables interrupts until the
         completion of the next instruction, so these two
         instructions are executed atomically.  This atomicity is
         important; otherwise, an interrupt could be handled
         between re-enabling interrupts and waiting for the next
         one to occur, wasting as much as one clock tick worth of
         time.

         See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
         7.11.1 "HLT Instruction". */
    asm volatile("sti; hlt"
                 :
                 :
                 : "memory");
  }
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread(thread_func *function, void *aux)
{
  ASSERT(function != NULL);

  intr_enable(); /* The scheduler runs with interrupts off. */
  function(aux); /* Execute the thread function. */
  thread_exit(); /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread *
running_thread(void)
{
  uint32_t *esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Because `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm("mov %%esp, %0"
      : "=g"(esp));
  return pg_round_down(esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool
is_thread(struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}


// todo:初始化nice和recent_cpu
/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread(struct thread *t, const char *name, int priority)
{
  ASSERT(t != NULL);
  ASSERT(PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT(name != NULL);

  memset(t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy(t->name, name, sizeof t->name);
  t->stack = (uint8_t *)t + PGSIZE;
  t->priority = priority;
  t->real_priority = priority;
  //TODO:INITIALIZED 初始化
  t->nice = 0;
  t->recent_cpu = INT_TO_FP(0);
  t->magic = THREAD_MAGIC;
  list_push_ordered(&all_list, &t->allelem, priority);
  //list_push_back(&all_list, &t->allelem);
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame(struct thread *t, size_t size)
{
  /* Stack data is always allocated in word-size units. */
  ASSERT(is_thread(t));
  ASSERT(size % sizeof(uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
struct thread *
next_thread_to_run(void)
{
  if (list_empty(&ready_list))
    return idle_thread;
  else
    return list_entry(list_pop_front(&ready_list), struct thread, elem);
}

/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_schedule() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
void thread_schedule_tail(struct thread *prev)
{
  struct thread *cur = running_thread();

  ASSERT(intr_get_level() == INTR_OFF);

  /* Mark us as running. */
  cur->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate();
#endif

  /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We don't fr    // {"alarm-multiple", test_alarm_multiple},
     initial_thread because its memory was not obtained via
     palloc().) */
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread)
  {
    ASSERT(prev != cur);
    palloc_free_page(prev);
  }
}

static void
wakeup_thread(void)
{
  if (list_empty(&sleep_list))
    return; //return if sleep_list is empty

  struct list_elem *e;
  struct thread *thread;
  e = list_begin(&sleep_list);
  while (e != list_end(&sleep_list))
  {
    thread = list_entry(e, struct thread, sleepelem);
    if (thread->sleep_end <= timer_ticks())
    {
      e = list_next(e);
      list_pop_front(&sleep_list);
      thread_unblock(thread);
    }
    else
    {
      break;
    }
  }
}

/* Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until thread_schedule_tail()
   has completed. */
static void
schedule(void)
{
  wakeup_thread();
  struct thread *cur = running_thread();
  struct thread *next = next_thread_to_run();
  struct thread *prev = NULL;

  ASSERT(intr_get_level() == INTR_OFF);
  ASSERT(cur->status != THREAD_RUNNING);
  ASSERT(is_thread(next));

  if (cur != next)
    prev = switch_threads(cur, next);
  thread_schedule_tail(prev);
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid(void)
{
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire(&tid_lock);
  tid = next_tid++;
  lock_release(&tid_lock);

  return tid;
}

void update_recent_cpu_signle()
{
  struct thread *cur = thread_current();
  if( cur != idle_thread) cur->recent_cpu = ADD_FP_INT(cur->recent_cpu,1);
}


void update_load_avg(){
  fp tmp_load_avg = DIVIDE_FP_INT(MULTI_FP_INT(load_avg,59),60);
  size_t cur_ready;
  if(thread_current() != idle_thread) cur_ready = list_size(&ready_list) + 1;
  else cur_ready = list_size(&ready_list);
  fp tmp_ready = DIVIDE_FP_INT(INT_TO_FP(cur_ready),60);
  //这里tmp——load——avg类型可能不太确定
  load_avg = ADD_FP_FP(tmp_load_avg,tmp_ready);
}

void update_recent_cpu()
{
  struct list_elem * e;
  struct thread *cur;
  for (e = list_begin(&all_list); e != list_end(&all_list); e = list_next(e)){
    cur = list_entry(e,struct thread,allelem);
    fp parameter = DIVIDE_FP(MULTI_FP_INT(load_avg,2),ADD_FP_INT(MULTI_FP_INT(load_avg,2),1));
    cur->recent_cpu = ADD_FP_INT(MULTI_FP(parameter,cur->recent_cpu),cur->nice);
  }
}

void update_priority(){
  struct list_elem * e;
  struct thread *cur;
  for (e = list_begin(&all_list); e != list_end(&all_list); e = list_next(e)){
    cur = list_entry(e,struct thread,allelem);
    fp tmp_cpu = DIVIDE_FP_INT(cur->recent_cpu,4);
    fp tmp_priority = SUB_FP_INT(SUB_INT_FP(PRI_MAX,tmp_cpu),2*cur->nice);
    cur->priority = FP_TO_INT_ZERO(tmp_priority);
  }
}

/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof(struct thread, stack);

/* 检查是否需要捐赠优先级 */
bool check_if_need_donation(int high_pri, int low_pri)
{
  if (high_pri <= low_pri)
    return false;
  struct list_elem *e;
  for (e = list_begin(&ready_list); e != list_end(&ready_list); e = list_next(e))
  {
    struct thread *thread = list_entry(e, struct thread, elem);
    if (thread->priority < high_pri && thread->priority > low_pri)
    {
      return true;
    }
  }
  return false;
}

/* 根据优先级将线程的执行次序提前 */
void thread_promote(struct thread *mThread)
{
  struct list_elem *e;

  while (&mThread->elem != list_front(&ready_list))
  {
    e = list_prev(&mThread->elem);
    struct thread *thr = list_entry(e, struct thread, elem);
    if (mThread->priority >= thr->priority)
    {
      //交换过程
      thr->elem.next = mThread->elem.next;
      mThread->elem.prev = thr->elem.prev;
      thr->elem.prev = &mThread->elem;
      mThread->elem.next = &thr->elem;
    }
    else
      break;
  }
}