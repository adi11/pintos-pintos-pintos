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

/* 两种方法：1.使用额外的链表，保存所有睡眠的进程的信息。（费空间）
 *         2.使用all_list，然后遍历寻找睡眠进程。（费时间）
 * 暂时使用第一种方法，链表为sleep_list。 */
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
    void *eip;                  /* Return address. */
    thread_func *function;      /* Function to call. */
    void *aux;                  /* Auxiliary data for function. */
  };

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

static fixedpoint load_avg;     /* load_avg. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);

/* 比较优先级函数 */
static bool
value_less (const struct list_elem *a_, const struct list_elem *b_,
            void *aux UNUSED)
{
  const struct thread *a = list_entry (a_, struct thread, elem);
  const struct thread *b = list_entry (b_, struct thread, elem);

  return a->priority < b->priority;
}

bool
thread_is_idle()
{
  return thread_current () == idle_thread;
}

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
void
thread_init (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);

  lock_init (&tid_lock);
  list_init (&ready_list);
  list_init (&all_list);

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();

  /* 初始化initial_thread的nice为0。*/
  initial_thread->nice = 0;
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) 
{
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) 
{
  struct thread *t = thread_current ();

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
  /* 判断时间片是否到达，如果到达则yield。round-robin */
  if (++thread_ticks >= TIME_SLICE)
    {
      /* 此函数稍候会调用thread_yield()函数 */
      intr_yield_on_return ();
    }
}

/* Prints thread statistics. */
void
thread_print_stats (void) 
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
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
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux) 
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;
  enum intr_level old_level;

  ASSERT (function != NULL);

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  /* 放到alllist里，处于阻塞状态 */
  init_thread (t, name, priority);
  tid = t->tid = allocate_tid ();

  /* Prepare thread for first run by initializing its stack.
     Do this atomically so intermediate values for the 'stack' 
     member cannot be observed. */
  old_level = intr_disable ();

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  intr_set_level (old_level);

  /* 放到就绪队列，更改状态为就绪 */
  thread_unblock (t);

  thread_yield();

  return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) 
{
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);

  thread_current ()->status = THREAD_BLOCKED;
  schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) 
{
  enum intr_level old_level;

  ASSERT (is_thread (t));

  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);

  /* 放到就绪队列 */
  list_push_back (&ready_list, &t->elem);
  t->status = THREAD_READY;
  intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void) 
{
  return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) 
{
  struct thread *t = running_thread ();
  
  /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4 kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
  ASSERT (is_thread (t));
  ASSERT (t->status == THREAD_RUNNING);

  return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) 
{
  return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) 
{
  ASSERT (!intr_context ());

#ifdef USERPROG
  process_exit ();
#endif

  /* Remove thread from all threads list, set our status to dying,
     and schedule another process.  That process will destroy us
     when it calls thread_schedule_tail(). */
  intr_disable ();
  list_remove (&thread_current()->allelem);
  thread_current ()->status = THREAD_DYING;
  schedule ();
  NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) 
{
  struct thread *cur = thread_current ();
  enum intr_level old_level;
  
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  if (cur != idle_thread) 
    {
      list_push_back (&ready_list, &cur->elem);
    }

  /* 标记当前线程为就绪状态，下一步schedule()将当前线程放入就绪队列，
   * 开始进行线程切换。 */
  cur->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level);
}

/* Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void
thread_foreach (thread_action_func *func, void *aux)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      func (t, aux);
    }
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) 
{
  /* 判断是否是多级反馈调度。*/
  if (!thread_mlfqs)
    {
      /* 如果不是被捐赠者 */
      if (!thread_current ()->is_donee)
        {
          thread_current ()->priority = new_priority;
          thread_current ()->ori_pri = new_priority;

          /* 设置优先级后立即进行调度，抢占方式。 */
          thread_yield ();
        }
      /* 如果是被捐赠者，当前设置的优先级不能立即生效，需等待变为非被捐赠者时进行变更。 */
      else
        {
          thread_current ()->ori_pri = new_priority;
        }
    }
}

/* 设置一个线程的优先级 modified base on thread_set_priority()，
 * 用于更改处于非运行状态的线程的优先级，方法返回前不掉用thread_yield()，
 * （与thread_set_priority方法不同，非抢占）。 */
void
thread_update_priority_with_thread (struct thread *thread,
    int new_priority)
{
  ASSERT (thread != NULL)

  thread->priority = new_priority;
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) 
{
  return thread_current ()->priority;
}

/* 更新进程优先级（针对mlfqs调度）
 * 公式：priority = PRI_MAX - (recent_cpu / 4) - (nice * 2)
 * 优先级要符合小于PRI_MAX并且大于PRI_MIN。
 */
void
thread_priority_update(struct thread *t, void *aux UNUSED)
{
  int priority = PRI_MAX
      - convert_fixedpoint_to_int(
          fixedpoint_divide_int(t->recent_cpu, 4))
      - (t->nice * 2);
  if (priority > PRI_MAX)
    {
      priority = PRI_MAX;
    }
  else if (priority < PRI_MIN)
    {
      priority = PRI_MIN;
    }
  thread_update_priority_with_thread (t, priority);
}

/*
  Sets the current thread’s nice value to new nice and recalculates
  the thread’s priority based on the new value. If the
  running thread no longer has the highest priority, yields.
*/
void
thread_set_nice (int nice UNUSED) 
{
  enum intr_level oldlevle = intr_disable();

  thread_current ()->nice = nice;

  //load_avg_update ();
  //thread_recent_cpu_update (thread_current (), NULL);
  thread_priority_update (thread_current (), NULL);

  intr_set_level (oldlevle);

  /* If the running thread no longer has the highest priority, yields. */
  struct list_elem *max_elem = list_max (&ready_list, value_less, NULL);
  struct thread *thread = list_entry (max_elem, struct thread, elem);
  if(thread_current ()->priority < thread->priority )
    {
      thread_yield();
    }
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void)
{
  return thread_current ()->nice;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) 
{
  return convert_fixedpoint_to_int (
      fixedpoint_multiply_int (load_avg, 100));
}

/* load_avg = (59/60)*load_avg + (1/60)*ready_threads.
 * (59/60)*(2^14) = 16,111(fixedpoint).
 * (1/60)*(2^14) = 273(fixedpoint).
 */
void
load_avg_update()
{
  int ready_threads = list_size(&ready_list);

  /*
  where ready threads is the number of threads that are either
  running or ready to run at time of update (not including the
  idle thread).
   */
  /* 如果当前运行的进程不是空闲进程，则将ready_threads数量+1. */
  if (!thread_is_idle ())
    {
      ready_threads += 1;
    }

  load_avg = fixedpoint_multiply (16111, load_avg)
      + fixedpoint_multiply_int (273, ready_threads);
}

/* Returns 100 times the current thread's recent_cpu value.
 * recent_cpu = (2*load_avg )/(2*load_avg + 1) * recent_cpu + nice .
 * */
int
thread_get_recent_cpu (void) 
{
  return convert_fixedpoint_to_int(
      fixedpoint_multiply_int(thread_current ()->recent_cpu, 100));
}

/* 更新进程recent_cpu。
 * recent_cpu = (2*load_avg )/(2*load_avg + 1) * recent_cpu + nice
 * */
void
thread_recent_cpu_update (struct thread *t, void *aux UNUSED)
{
  /*
   * You may need to think about the order of calculations in this
   * formula. We recommend computing the coefficient of recent cpu
   * first, then multiplying. Some students have reported that
   * multiplying load avg by recent cpu directly can cause overflow.
   */

  fixedpoint double_load_avg = fixedpoint_multiply_int (load_avg, 2);
  fixedpoint double_load_avg_plus_one =
      fixedpoint_add_int (double_load_avg, 1);
  fixedpoint temp = fixedpoint_divide (double_load_avg,
      double_load_avg_plus_one);
  temp = fixedpoint_multiply (temp, t->recent_cpu);
  t->recent_cpu = temp + convert_int_to_fixedpoint (t->nice);
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
idle (void *idle_started_ UNUSED) 
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;) 
    {
      /* Let someone else run. */
      intr_disable ();
      thread_block ();

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
      asm volatile ("sti; hlt" : : : "memory");
    }
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) 
{
  ASSERT (function != NULL);

  intr_enable ();       /* The scheduler runs with interrupts off. */
  function (aux);       /* Execute the thread function. */
  thread_exit ();       /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread *
running_thread (void) 
{
  uint32_t *esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Because `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority)
{
  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  t->priority = priority;

  /* 初始化睡眠ticks数量 */
  //t->sleep_ticks = 0;

  /* 初始化最原始的优先级（保存） */
  t->ori_pri = priority;

  /* 初始化为不是优先级被捐赠者。 */
  t->is_donee = false;

  /* 初始化除initial进程外其它进程nice为父亲进程的nice. */
  if (is_thread (running_thread ())
      && thread_current () != initial_thread)
    {
      t->nice = thread_current ()->nice;

      /* 进程初始化时recent_cpu的值肯定为0，所以在下面算式省略掉。 */
      if(thread_mlfqs)
        {
          int priority = PRI_MAX - t->nice * 2;
          if (priority > PRI_MAX)
            {
              priority = PRI_MAX;
            }
          t->priority = priority;
        }

      /* The initial value of recent cpu is 0 in the first
       * thread created, or the parent’s value in other new threads.*/
      t->recent_cpu = thread_current ()->recent_cpu;
    }
  else
    {
      t->recent_cpu = 0;
    }

  /* 初始化持有锁链表 */
  list_init (&t->hold_lock_list);

  /* 初始化请求的锁链表 */
  list_init (&t->acquire_lock_list);

  t->magic = THREAD_MAGIC;
  list_push_back (&all_list, &t->allelem);
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame (struct thread *t, size_t size) 
{
  /* Stack data is always allocated in word-size units. */
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) 
{
  if (list_empty (&ready_list))
    {
      return idle_thread;
    }
  else
    {
      /* 获取就绪队列中优先级最大的线程，从就绪队列中移除，返回该线程。
       * 如果有多个优先级最大的线程，则选择最早放入就绪队列中的线程，
       * 参见list_max函数注释。 */
      struct list_elem *max_elem = list_max (&ready_list, value_less, NULL);
      list_remove (max_elem);
      return list_entry (max_elem, struct thread, elem);
    }
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
void
thread_schedule_tail (struct thread *prev)
{
  struct thread *cur = running_thread ();
  
  ASSERT (intr_get_level () == INTR_OFF);

  /* Mark us as running. */
  cur->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate ();
#endif

  /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We don't free
     initial_thread because its memory was not obtained via
     palloc().) */
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread) 
    {
      ASSERT (prev != cur);
      palloc_free_page (prev);
    }
}

/* Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until thread_schedule_tail()
   has completed. */
static void
schedule (void) 
{
  struct thread *cur = running_thread ();
  struct thread *next = next_thread_to_run ();
  struct thread *prev = NULL;

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (cur->status != THREAD_RUNNING);
  ASSERT (is_thread (next));

  /* 切换next_thread为cur_thread。cur_thread为pre_thread（返回值），
   * prev用于下一个thread_schedule_tail()函数，如果dying进行销毁。 */
  if (cur != next)
    prev = switch_threads (cur, next);

  /* 完成线程切换，将next_thread标记为运行状态。 */
  thread_schedule_tail (prev);
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) 
{
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire (&tid_lock);
  tid = next_tid++;
  lock_release (&tid_lock);

  return tid;
}

/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);
