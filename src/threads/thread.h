#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "synch.h"

/* States in a thread's life cycle. */
enum thread_status
  {
    THREAD_RUNNING,     /* Running thread. */
    THREAD_READY,       /* Not running but ready to run. */
    THREAD_BLOCKED,     /* Waiting for an event to trigger. */
    THREAD_DYING        /* About to be destroyed. */
  };

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

#define FDMAX 128                       /* Max value of fd table */

/* A kernel thread or user process.

   Each thread structure is stored in its own 4 kB page.  The
   thread structure itself sits at the very bottom of the page
   (at offset 0).  The rest of the page is reserved for the
   thread's kernel stack, which grows downward from the top of
   the page (at offset 4 kB).  Here's an illustration:

        4 kB +---------------------------------+
             |          kernel stack           |
             |                |                |
             |                |                |
             |                V                |
             |         grows downward          |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             +---------------------------------+
             |              magic              |
             |                :                |
             |                :                |
             |               name              |
             |              status             |
        0 kB +---------------------------------+

   The upshot of this is twofold:

      1. First, `struct thread' must not be allowed to grow too
         big.  If it does, then there will not be enough room for
         the kernel stack.  Our base `struct thread' is only a
         few bytes in size.  It probably should stay well under 1
         kB.

      2. Second, kernel stacks must not be allowed to grow too
         large.  If a stack overflows, it will corrupt the thread
         state.  Thus, kernel functions should not allocate large
         structures or arrays as non-static local variables.  Use
         dynamic allocation with malloc() or palloc_get_page()
         instead.

   The first symptom of either of these problems will probably be
   an assertion failure in thread_current(), which checks that
   the `magic' member of the running thread's `struct thread' is
   set to THREAD_MAGIC.  Stack overflow will normally change this
   value, triggering the assertion.  (So don't add elements below 
   THREAD_MAGIC.)
*/
/* The `elem' member has a dual purpose.  It can be an element in
   the run queue (thread.c), or it can be an element in a
   semaphore wait list (synch.c).  It can be used these two ways
   only because they are mutually exclusive: only a thread in the
   ready state is on the run queue, whereas only a thread in the
   blocked state is on a semaphore wait list. */

/* There are a few new elements in the thread struct to handle
   user programs properly. They are described below:
    
  struct file *fds[FDMAX];
    A list of file structs. The *fds is the file descriptor
    table. This allows us to keep track of what files a process
    has opened.

  struct file *self_executable;
    If the file being opened is loaded as a process, the process
    holds a special file struct for itself to deny writes to that
    file.

  uint32_t fd_size;
    Number of elements currently in the file descriptor table.
    
  struct thread *parent;
    When the thread is a "child" it will have a reference thread
    struct back to its own parent.

  struct semaphore exec_synchro;
  struct semaphore exec_reap;
    These semaphores allow a process to call exec and to reap
    the PID of the new process if it successfully loads.


  struct semaphore sys_wait_sema;
  struct semaphore sys_wait_reap;
    These semaphores correspond with the sys_wait() system call.
    They allow parent processes to wait on child processes, and 
    for a child process to block until its parent process is ready
    to retrieve its exit status.

  struct list child_list;
  struct list_elem child_elem;
    This list of child processes allows a parent to know which
    processes are its children. If a process is not in this list,
    it is not a child, and thus cannot be waited upon.

  int exit_status;
    The exit status passed to the sys_exit() system call.
  int load_status; 
    A simple flag to check if a process has loaded successfully. */


struct thread
  {
    /* Owned by thread.c. */
    tid_t tid;                          /* Thread identifier. */
    enum thread_status status;          /* Thread state. */
    char name[16];                      /* Name (for debugging purposes). */
    uint8_t *stack;                     /* Saved stack pointer. */
    int priority;                       /* Priority. */
    struct list_elem allelem;           /* List element for all threads list. */

    /* Shared between thread.c and synch.c. */
    struct list_elem elem;              /* List element. */

#ifdef USERPROG
    /* Owned by userprog/process.c. */
    uint32_t *pagedir;                  /* Page directory. */
#endif

    /* File Struct for File Descriptors & Directories */
    struct file *fds[FDMAX];
    struct file *self_executable;
    uint32_t fd_size;
    struct dir *dir;
    
    /* Parent thread */
    struct thread *parent;

    /* Semaphore for EXEC */
    struct semaphore exec_synchro;
    struct semaphore exec_reap;

    /* Data structures for parent/child processes */
    struct semaphore sys_wait_sema;
    struct semaphore sys_wait_reap;

    struct list child_list;
    struct list_elem child_elem;

    int exit_status;
    int load_status;

    /* Link to current directory */
    struct dir *cur_dir;

    /* Owned by thread.c. */
    unsigned magic;                     /* Detects stack overflow. */

  };

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

/* Performs some operation on thread t, given auxiliary data AUX. */
typedef void thread_action_func (struct thread *t, void *aux);
void thread_foreach (thread_action_func *, void *);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

struct thread *get_child (pid_t);
void set_orphan (struct thread *);


#endif /* threads/thread.h */
