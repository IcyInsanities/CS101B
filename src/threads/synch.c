/*! \file synch.c
 *
 * Implementation of various thread synchronization primitives.
 */

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

#ifndef max
	#define max( a, b ) ( ((a) > (b)) ? (a) : (b) )
#endif

#ifndef min
	#define min( a, b ) ( ((a) < (b)) ? (a) : (b) )
#endif

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/*! Initializes semaphore SEMA to VALUE.  A semaphore is a
    nonnegative integer along with two atomic operators for
    manipulating it:

    - down or "P": wait for the value to become positive, then
      decrement it.

    - up or "V": increment the value (and wake up one waiting
      thread, if any). */
void sema_init(struct semaphore *sema, unsigned value) {
    ASSERT(sema != NULL);

    sema->value = value;
    list_init(&sema->waiters);
}

/*! Down or "P" operation on a semaphore.  Waits for SEMA's value
    to become positive and then atomically decrements it.

    This function may sleep, so it must not be called within an
    interrupt handler.  This function may be called with
    interrupts disabled, but if it sleeps then the next scheduled
    thread will probably turn interrupts back on. */
void sema_down(struct semaphore *sema) {
    enum intr_level old_level;

    ASSERT(sema != NULL);
    ASSERT(!intr_context());

    old_level = intr_disable();
    while (sema->value == 0) {
        list_push_back(&sema->waiters, &thread_current()->elem);
        thread_block();
    }
    sema->value--;
    intr_set_level(old_level);
}

/*! Down or "P" operation on a semaphore, but only if the
    semaphore is not already 0.  Returns true if the semaphore is
    decremented, false otherwise.

    This function may be called from an interrupt handler. */
bool sema_try_down(struct semaphore *sema) {
    enum intr_level old_level;
    bool success;

    ASSERT(sema != NULL);

    old_level = intr_disable();
    if (sema->value > 0) {
        sema->value--;
        success = true;
    }
    else {
      success = false;
    }
    intr_set_level(old_level);

    return success;
}

/*! This returns the thread with maximal priority waiting for a semaphore */
struct list_elem * sema_max_priority_wait(struct semaphore *sema) {
    struct list_elem *curr, *e_max;
    struct thread *t, *t_max;

    ASSERT(!list_empty(&sema->waiters))

    e_max = list_begin(&sema->waiters);
    t_max = list_entry (e_max, struct thread, elem);
    for (curr = list_next(e_max); curr != list_end(&sema->waiters);
         curr = list_next(curr))
    {
        t = list_entry (curr, struct thread, elem);
        if (t->priority > t_max->priority)
        {
            e_max = curr;
            t_max = t;
        }
    }
    return e_max;
}

/*! Up or "V" operation on a semaphore.  Increments SEMA's value
    and wakes up one thread of those waiting for SEMA, if any.

    This function may be called from an interrupt handler. */
void sema_up(struct semaphore *sema) {
    enum intr_level old_level;

    ASSERT(sema != NULL);

    old_level = intr_disable();
    // Ensure that highest priority waiting thread is unblocked.
    if (!list_empty(&sema->waiters)) {
        struct list_elem *e_max;
        struct thread *t_max;

        e_max = sema_max_priority_wait(sema);
        t_max = list_entry (e_max, struct thread, elem);

        list_remove(e_max);
        thread_unblock(t_max);
    }
    sema->value++;

    if (thread_mlfqs)
    {
        thread_update_priority();
    }

    intr_set_level(old_level);

    // See if higher priority thread can now run
    thread_yield();
}

static void sema_test_helper(void *sema_);

/*! Self-test for semaphores that makes control "ping-pong"
    between a pair of threads.  Insert calls to printf() to see
    what's going on. */
void sema_self_test(void) {
    struct semaphore sema[2];
    int i;

    printf("Testing semaphores...");
    sema_init(&sema[0], 0);
    sema_init(&sema[1], 0);
    thread_create("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
    for (i = 0; i < 10; i++) {
        sema_up(&sema[0]);
        sema_down(&sema[1]);
    }
    printf ("done.\n");
}

/*! Thread function used by sema_self_test(). */
static void sema_test_helper(void *sema_) {
    struct semaphore *sema = sema_;
    int i;

    for (i = 0; i < 10; i++) {
        sema_down(&sema[0]);
        sema_up(&sema[1]);
    }
}

/*! Initializes LOCK.  A lock can be held by at most a single
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
void lock_init(struct lock *lock) {
    ASSERT(lock != NULL);

    lock->holder = NULL;
    sema_init(&lock->semaphore, 1);
    lock->priority = PRI_MIN;
}

/*! Acquires LOCK, sleeping until it becomes available if
    necessary.  The lock must not already be held by the current
    thread.

    This function may sleep, so it must not be called within an
    interrupt handler.  This function may be called with
    interrupts disabled, but interrupts will be turned back on if
    we need to sleep. */
// TODO: MODIFY
void lock_acquire(struct lock *lock) {

    struct thread *t = thread_current();

    ASSERT(lock != NULL);
    ASSERT(!intr_context());
    ASSERT(!lock_held_by_current_thread(lock));

    // Attempting to acquire lock
    thread_update_lock_to_acquire(t, lock);
    ASSERT(t->lock_to_acquire != NULL); // DEBUG:

    // If some one has a lock, donate priority to them
    if (lock->holder != NULL) {
        lock_update_priority(lock, t->priority);
    }

    sema_down(&lock->semaphore);

    // Have acquired lock
    lock->holder = t;

    // Update the lock priority to that of new holder, as it should be the
    // highest prority
    lock->priority = (lock->holder)->priority;
    //printf("updated lock priority: %d\n", lock->priority);

    // Add lock to list of locks held by current thread
    thread_acquire_lock(lock);

    // Ensure no longer attempting to acquire lock
    ASSERT(t->lock_to_acquire == NULL); // DEBUG:

}

/*! Tries to acquires LOCK and returns true if successful or false
    on failure.  The lock must not already be held by the current
    thread.

    This function will not sleep, so it may be called within an
    interrupt handler. */
// TODO: MODIFY
bool lock_try_acquire(struct lock *lock) {
    bool success;
    struct thread *t = thread_current();

    ASSERT(lock != NULL);
    ASSERT(!lock_held_by_current_thread(lock));

    success = sema_try_down(&lock->semaphore);

    if (success) {
        // Have acquired lock
        lock->holder = t;
        // Update the lock priority to that of new holder, as it should be the
        // highest prority
        lock->priority = (lock->holder)->priority;
        // Add lock to list of locks held by current thread
        thread_acquire_lock(lock);
    } else {

        // If failed to acquire, do nothing
    }

    return success;
}

/*! Releases LOCK, which must be owned by the current thread.

    An interrupt handler cannot acquire a lock, so it does not
    make sense to try to release a lock within an interrupt
    handler. */
// TODO: MODIFY
void lock_release(struct lock *lock) {

    int lock_priority;
    struct thread *t = thread_current();
    ASSERT(lock != NULL);
    ASSERT(lock_held_by_current_thread(lock));

    ASSERT(list_size(&((lock->holder)->locks_held)) != 0);
    // Release the lock
    thread_release_lock(lock);

    // Get the new highest priority and update the priority of the holder
    lock_priority = thread_lock_max_priority(t);
    /*
    if (lock_priority > t->orig_priority) {
        t->priority = lock->priority;
    } else {
        t->priority = t->orig_priority;
    }
    */

    // Set lock priority to lowest, since no one holds it
    lock->priority = PRI_MIN;

    lock->holder = NULL;

    sema_up(&lock->semaphore);
}

/*! Returns true if the current thread holds LOCK, false
    otherwise.  (Note that testing whether some other thread holds
    a lock would be racy.) */
// TODO: MODIFY
bool lock_held_by_current_thread(const struct lock *lock) {
    ASSERT(lock != NULL);

    return lock->holder == thread_current();
}

void lock_update_priority(struct lock *lock, int priority) {

    // New priority of lock is highest of new priority and passed
    lock->priority = max(priority, lock->priority);

    // Update the priority of the holder
    thread_lock_set_priority(lock->priority, lock->holder);

}

/*! One semaphore in a list. */
struct semaphore_elem {
    struct list_elem elem;              /*!< List element. */
    struct semaphore semaphore;         /*!< This semaphore. */
};

/*! Initializes condition variable COND.  A condition variable
    allows one piece of code to signal a condition and cooperating
    code to receive the signal and act upon it. */
void cond_init(struct condition *cond) {
    ASSERT(cond != NULL);

    list_init(&cond->waiters);
}

/*! Atomically releases LOCK and waits for COND to be signaled by
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
void cond_wait(struct condition *cond, struct lock *lock) {
    struct semaphore_elem waiter;

    ASSERT(cond != NULL);
    ASSERT(lock != NULL);
    ASSERT(!intr_context());
    ASSERT(lock_held_by_current_thread(lock));

    sema_init(&waiter.semaphore, 0);
    list_push_back(&cond->waiters, &waiter.elem);
    lock_release(lock);
    sema_down(&waiter.semaphore);
    lock_acquire(lock);
}

/*! If any threads are waiting on COND (protected by LOCK), then
    this function signals one of them to wake up from its wait.
    LOCK must be held before calling this function.

    An interrupt handler cannot acquire a lock, so it does not
    make sense to try to signal a condition variable within an
    interrupt handler. */
void cond_signal(struct condition *cond, struct lock *lock UNUSED) {
    ASSERT(cond != NULL);
    ASSERT(lock != NULL);
    ASSERT(!intr_context ());
    ASSERT(lock_held_by_current_thread (lock));

    if (!list_empty(&cond->waiters))
    {
        // Ensure that highest priority waiting thread is signaled.
        struct list_elem *curr, *e_max;
        struct list_elem *s, *s_max;
        struct semaphore *sema, *sema_max;
        struct thread *t, *t_max;

        e_max = list_begin(&cond->waiters);
        sema_max = &list_entry (e_max, struct semaphore_elem, elem)->semaphore;
        s_max = sema_max_priority_wait(sema_max);
        t_max = list_entry (s_max, struct thread, elem);
        for (curr = list_next(e_max); curr != list_end(&cond->waiters);
             curr = list_next(curr))
        {
            sema = &list_entry (curr, struct semaphore_elem, elem)->semaphore;
            s = sema_max_priority_wait(sema);
            t = list_entry (s, struct thread, elem);
            if (t->priority > t_max->priority)
            {
                e_max = curr;
                sema_max = sema;
                s_max = s;
                t_max = t;
            }
        }
        list_remove(e_max);
        sema_up(sema_max);
    }
}

/*! Wakes up all threads, if any, waiting on COND (protected by
    LOCK).  LOCK must be held before calling this function.

    An interrupt handler cannot acquire a lock, so it does not
    make sense to try to signal a condition variable within an
    interrupt handler. */
void cond_broadcast(struct condition *cond, struct lock *lock) {
    ASSERT(cond != NULL);
    ASSERT(lock != NULL);

    while (!list_empty(&cond->waiters))
        cond_signal(cond, lock);
}

