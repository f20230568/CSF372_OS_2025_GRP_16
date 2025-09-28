#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* --- New helper function for priority comparison --- */
/** Returns true if thread A has a higher priority than thread B.
    Used for sorting lists in descending order of priority. */
static bool
waiter_priority_less(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
    return list_entry(a, struct thread, elem)->priority > list_entry(b, struct thread, elem)->priority;
}
/* --- End of new function --- */

/** Initializes semaphore SEMA to VALUE. */
void
sema_init (struct semaphore *sema, unsigned value) 
{
  ASSERT (sema != NULL);

  sema->value = value;
  list_init (&sema->waiters);
}

/** Down or "P" operation on a semaphore. */
void
sema_down (struct semaphore *sema) 
{
  enum intr_level old_level;

  ASSERT (sema != NULL);
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  while (sema->value == 0) 
    {
      /* --- Modified for priority scheduling --- */
      list_insert_ordered(&sema->waiters, &thread_current()->elem, waiter_priority_less, NULL);
      /* --- End of modification --- */
      thread_block ();
    }
  sema->value--;
  intr_set_level (old_level);
}

/** Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise. */
bool
sema_try_down (struct semaphore *sema) 
{
  enum intr_level old_level;
  bool success;

  ASSERT (sema != NULL);

  old_level = intr_disable ();
  if (sema->value > 0) 
    {
      sema->value--;
      success = true; 
    }
  else
    success = false;
  intr_set_level (old_level);

  return success;
}

/** Up or "V" operation on a semaphore. */
void
sema_up (struct semaphore *sema) 
{
  enum intr_level old_level;

  ASSERT (sema != NULL);

  old_level = intr_disable ();
  if (!list_empty (&sema->waiters)) 
    {
      /* --- Modified for priority scheduling --- */
      /* list_pop_front will now get the highest-priority waiter. */
      struct thread *t = list_entry(list_pop_front(&sema->waiters), struct thread, elem);
      thread_unblock(t);
      /* --- End of modification --- */
    }
  sema->value++;

  /* --- New part for preemption --- */
  thread_yield_if_not_highest();
  /* --- End of new part --- */
  
  intr_set_level (old_level);
}

static void sema_test_helper (void *sema_);

/** Self-test for semaphores that makes control "ping-pong"
   between a pair of threads. */
void
sema_self_test (void) 
{
  struct semaphore sema[2];
  int i;

  printf ("Testing semaphores...");
  sema_init (&sema[0], 0);
  sema_init (&sema[1], 0);
  thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
  for (i = 0; i < 10; i++) 
    {
      sema_up (&sema[0]);
      sema_down (&sema[1]);
    }
  printf ("done.\n");
}

/** Thread function used by sema_self_test(). */
static void
sema_test_helper (void *sema_) 
{
  struct semaphore *sema = sema_;
  int i;

  for (i = 0; i < 10; i++) 
    {
      sema_down (&sema[0]);
      sema_up (&sema[1]);
    }
}

/** Initializes LOCK. */
void
lock_init (struct lock *lock)
{
  ASSERT (lock != NULL);

  lock->holder = NULL;
  sema_init (&lock->semaphore, 1);
}

/** Acquires LOCK, sleeping until it becomes available if
   necessary. */
void
lock_acquire (struct lock *lock)
{
  /* --- Modified for priority donation --- */
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (!lock_held_by_current_thread (lock));
  
  struct thread *cur = thread_current ();

  if (lock->holder != NULL)
    {
      cur->waiting_on_lock = lock;
      thread_donate_priority (lock->holder);
    }

  sema_down (&lock->semaphore);
  
  cur->waiting_on_lock = NULL;
  list_push_back (&cur->locks_held, &lock->elem);
  lock->holder = cur;
  /* --- End of modification --- */
}

/** Tries to acquires LOCK and returns true if successful or false
   on failure. */
bool
lock_try_acquire (struct lock *lock)
{
  bool success;

  ASSERT (lock != NULL);
  ASSERT (!lock_held_by_current_thread (lock));

  success = sema_try_down (&lock->semaphore);
  if (success) {
    lock->holder = thread_current ();
    /* --- New part --- */
    list_push_back(&thread_current()->locks_held, &lock->elem);
    /* --- End of new part --- */
  }
  return success;
}

/** Releases LOCK, which must be owned by the current thread. */
void
lock_release (struct lock *lock) 
{
  /* --- Modified for priority donation --- */
  ASSERT (lock != NULL);
  ASSERT (lock_held_by_current_thread (lock));

  list_remove(&lock->elem);
  lock->holder = NULL;
  
  thread_recalculate_priority(thread_current());

  sema_up (&lock->semaphore);
  /* --- End of modification --- */
}

/** Returns true if the current thread holds LOCK, false
   otherwise. */
bool
lock_held_by_current_thread (const struct lock *lock) 
{
  ASSERT (lock != NULL);
  return lock->holder == thread_current ();
}

/** One semaphore in a list. */
struct semaphore_elem 
  {
    struct list_elem elem;      /**< List element. */
    struct semaphore semaphore; /**< This semaphore. */
  };

/** Initializes condition variable COND. */
void
cond_init (struct condition *cond)
{
  ASSERT (cond != NULL);
  list_init (&cond->waiters);
}

/** Atomically releases LOCK and waits for COND to be signaled. */
void
cond_wait (struct condition *cond, struct lock *lock) 
{
  struct semaphore_elem waiter;

  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));
  
  sema_init (&waiter.semaphore, 0);
  
  /* --- Modified for priority scheduling --- */
  list_insert_ordered (&cond->waiters, &waiter.elem, 
                       (list_less_func *) &waiter_priority_less, NULL);
  /* --- End of modification --- */

  lock_release (lock);
  sema_down (&waiter.semaphore);
  lock_acquire (lock);
}

/** If any threads are waiting on COND, signals one of them. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) 
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));

  if (!list_empty (&cond->waiters)) 
    {
      /* --- Modified for priority scheduling --- */
      /* Wakes up the highest-priority waiter. */
      sema_up (&list_entry (list_pop_front (&cond->waiters),
                            struct semaphore_elem, elem)->semaphore);
      /* --- End of modification --- */
    }
}

/** Wakes up all threads, if any, waiting on COND. */
void
cond_broadcast (struct condition *cond, struct lock *lock) 
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);

  while (!list_empty (&cond->waiters))
    cond_signal (cond, lock);
}