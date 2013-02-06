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

#ifndef max
	#define max( a, b ) ( ((a) > (b)) ? (a) : (b) )
#endif

#ifndef min
	#define min( a, b ) ( ((a) < (b)) ? (a) : (b) )
#endif

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
struct list all_list;

/* List of all processes that are blocked from a sleep call. */
static struct list sleep_list;

/* Idle thread. */
struct thread *idle_thread;

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

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

/* Store the average system load, done as a fixed point number */
int64_t load_avg = 0;
#define DECIMAL_BITS    14
/* Constants for convenient fixed point arithmetic and functions using constant
   values */
#define FIXP_F        (int64_t) (1 << DECIMAL_BITS) 
#define FIXP_59DIV60  (int64_t) (0xFBBBBBBB >> (32-DECIMAL_BITS))
#define FIXP_01DIV60  (int64_t) (0x04444444 >> (32-DECIMAL_BITS))

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

// TODO: update to match struct
void
thread_init (void)
{
  ASSERT (intr_get_level () == INTR_OFF);

  lock_init (&tid_lock);
  list_init (&ready_list);
  list_init (&all_list);
  list_init (&sleep_list);

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();

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
  
  /* Increment the recent_cpu for the running thread */
  if(t != idle_thread)
  {
    t->recent_cpu += FIXP_F;
  }

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
    intr_yield_on_return ();

}

void thread_init_vals(void)
{
    load_avg = 273;

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

  ASSERT (function != NULL);

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread (t, name, priority);
  tid = t->tid = allocate_tid ();

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

  /* Add to run queue. */
  thread_unblock (t);
  
  /* Call scheduler */
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
  list_push_back (&ready_list, &t->elem);
  t->status = THREAD_READY;
  intr_set_level (old_level);
}

/* Puts the current thread to sleep for the given amount of timer ticks. The
   thread will be automatically awoken when that number of ticks has passed.

   Thus function expects to be called from timer_sleep generally.
 */
void
thread_sleep (int64_t ticks)
{
  enum intr_level old_level;
  struct thread *t = thread_current ();

  ASSERT(intr_get_level() == INTR_ON);

  /* Set the sleep time in the current thread and add to sleeping list */
  t->sleep_count = ticks;
  old_level = intr_disable ();  /* Disable intr so timer won't start early */
  list_push_back (&sleep_list, &t->elem);

  /* Set thread to blocked */
  thread_block ();

  /* Restore previous interrupt setting */
  intr_set_level (old_level);
}

/* Goes through the list of sleeping thread to decrement the time left to sleep
   and awakens them automatically when the time expires. */
void
thread_check_awaken (void)
{
  struct thread *t;
  struct list_elem *del, *curr = list_begin (&sleep_list);

  /* Go through list of sleeping threads */
  while (curr != list_end (&sleep_list))
  {
    t = list_entry (curr, struct thread, elem);
    --(t->sleep_count);
    /* Remove thread if time expired and unblock*/
    if (t->sleep_count == 0)
    {
      del = curr;
      curr = list_next (curr);
      list_remove(del);
      thread_unblock(t);
    }
    /* Move to next thread otherwise */
    else
    {
      curr = list_next (curr);
    }
  }
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
    list_push_back (&ready_list, &cur->elem);
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
    // Disable if advanced scheduler enabled
    if (thread_mlfqs)
    {
        return;
    }

    struct thread *t_curr = thread_current();

    // Update the priority of the thread
    t_curr->orig_priority = new_priority;
    
    // Set priority to max of donations or the actual priority
    t_curr->priority = thread_lock_max_priority(t_curr);
    t_curr->priority = (new_priority > t_curr->priority) ? new_priority : t_curr->priority;
    
    // If there is a lock to aquire, update its priority
    if (t_curr->lock_to_acquire != NULL) {
        lock_update_priority(t_curr->lock_to_acquire, t_curr->priority);
    }
    
    /* Find highest priority thread */
    thread_yield();
}

/* Sets the current thread's acting priority to NEW_PRIORITY. */
void
thread_lock_set_priority (int new_priority, struct thread *t)
{

    // If the priority has changed, propagate it to acquiring lock
    if (new_priority > t->priority) {
        // Update the priority of the thread
        t->priority = new_priority;

        // If there is a lock to aquire, update its priority
        if (t->lock_to_acquire != NULL) {
            lock_update_priority(t->lock_to_acquire, new_priority);
        }
    }
}

/* Returns the current thread's priority. */
int
thread_get_priority (void)
{ 
  return thread_current()->priority;
}

void thread_update_priority_indiv (struct thread* t, void* aux)
{
  int priority;

  priority = PRI_MAX;
  priority -= t->recent_cpu / FIXP_F / 4;
  priority -= t->nice * 2;
  
  // Bound between PRI_MIN and PRI_MAX
  priority = (priority < PRI_MIN) ? PRI_MIN : priority;
  priority = (priority > PRI_MAX) ? PRI_MAX : priority;

  t->priority = priority;

}

void thread_update_priority(void)
{
    thread_foreach(thread_update_priority_indiv, NULL);
}

bool thread_priority_less(struct list_elem* A, struct list_elem* B,
                                  void* aux)
{
    int priorityA;
    int priorityB;

    priorityA = list_entry(A, struct thread, elem)->priority;
    priorityB = list_entry(B, struct thread, elem)->priority;

    return priorityA < priorityB;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice)
{
  thread_current()->nice = nice;
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void)
{
  return thread_current()->nice;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void)
{
    // Truncate down to integer and return
    return 100 * load_avg / FIXP_F;
}

// Updates the value of load_avg
void thread_update_load_avg (void)
{
  //printf("load_avg %ld", load_avg);

  // Compute new value of the system load average
  load_avg *= FIXP_59DIV60; // load_avg *= 59/60 as fixed point
  load_avg /= FIXP_F;
  // load_avg += 1/60 * ready_theads
  load_avg += FIXP_01DIV60 * (int64_t)list_size(&ready_list);

  //printf("Queue len: %d", list_size(&ready_list));
  
  // If we are currently running a thread
  // TODO: STEVEN CHECKS IF THIS WORKS
  if (running_thread() != idle_thread)
  {
    // Account for the running thread
    load_avg += FIXP_01DIV60;
    //printf(" running");
  }
  
  //printf("\n");
  
  //printf("New is %ld\n", load_avg);
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void)
{
    // Truncate down to integer and return
    return 100 * thread_current()->recent_cpu / FIXP_F;
}

// Update the value for recent_cpu
void thread_update_recent_cpu_indiv (struct thread* t, void* aux)
{
  int64_t recent_cpu;

  // Fetch the current recent_cpu value
  recent_cpu = t->recent_cpu;
    
  // Update the value
  recent_cpu *= (2*load_avg);
  recent_cpu /= (2*load_avg + 1*FIXP_F);
  recent_cpu += t->nice * FIXP_F;
    
  // Put it back in the thread struct
  t->recent_cpu = recent_cpu;
}

void thread_update_recent_cpu()
{
    thread_foreach(thread_update_recent_cpu_indiv, NULL);
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
    enum intr_level old_level;
    
    ASSERT (t != NULL);
    ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
    ASSERT (name != NULL);
    
    memset (t, 0, sizeof *t);
    t->status = THREAD_BLOCKED;
    strlcpy (t->name, name, sizeof t->name);
    t->stack = (uint8_t *) t + PGSIZE;
    t->magic = THREAD_MAGIC;
    
    // Initially, original priority is same as working priority
    t->priority = priority;
    t->orig_priority = priority;
    
    // Initialize the list of locks held
    list_init (&(t->locks_held));
    
    // Initially no lock held
    t->lock_to_acquire = NULL;
    
    old_level = intr_disable ();
    list_push_back (&all_list, &t->allelem);
    intr_set_level (old_level);
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
  struct list_elem *curr, *max = NULL;
  struct thread *t, *t_max;
  int priority;
  
  if (list_empty (&ready_list))
    return idle_thread;
  // Use priority donation so take absolute maximum priority thread
  // Works with advanced scheduler as takes first element of the highest
  // priority level, which is removed. On yielding, it will be pushed onto the
  // end of the list.
  else
  {
    // Search for maximum priority thread
    max = list_max(&ready_list, thread_priority_less, NULL);
    t_max = list_entry(max, struct thread, elem);
    list_remove(max);
    return t_max;
  }

   // return list_entry (list_pop_front (&ready_list), struct thread, elem);
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

  if (cur != next)
    prev = switch_threads (cur, next);
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

// TODO: Debug this
/* Return the highest priority among locks held */
int
thread_lock_max_priority (struct thread *t)
{

    struct list_elem *i;
    size_t num_locks_held = list_size (&(t->locks_held));
    int max_priority = PRI_MIN;

    // Loop through all the locks held by the thread and find the highest
    // priority
    for (i = list_begin (&(t->locks_held)); i != list_end (&(t->locks_held));
            i = list_next (i)) {
    
        struct lock *current_lock = list_entry (i, struct lock, elem);
        
        // If priority of lock is higher than previous ones found, record it
        if (current_lock->priority > max_priority) {
            max_priority = current_lock->priority;
            //printf("found a higher priority!\n");
        }

    }
    return max_priority;
}

// TODO: write function to update lock_to_acquire
void
thread_update_lock_to_acquire (struct thread *t, struct lock *l)
{
    // Update the lock to acquire
    t->lock_to_acquire = l;
}

// TODO: need functions to add locks from locks_held list
void
thread_acquire_lock (struct lock *l)
{

    struct thread *t = thread_current();
    t->lock_to_acquire = NULL;
    
    // Add lock to the list of locks held
    list_push_back(&(t->locks_held), &(l->elem));
}

// TODO: need functions to remove locks from locks_held list
void
thread_release_lock (struct lock *l)
{
    // Only current thread could be releasing a lock
    struct thread *t = thread_current();

    // Remove released lock from list of held locks
    ASSERT(l != NULL);
    list_remove(&(l->elem));
    // Update priority based on remaining locks
    t->priority = thread_lock_max_priority(t);

    t->priority = (t->orig_priority > t->priority) ? t->orig_priority : t->priority;

}
