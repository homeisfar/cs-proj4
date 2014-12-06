#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "lib/user/syscall.h"
#include "pagedir.h"
#include "devices/shutdown.h"
#include "process.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "lib/kernel/console.h"
#include "threads/synch.h"
#include "devices/input.h"
#include "filesys/directory.h"
#include "filesys/inode.h"

static void syscall_handler (struct intr_frame *);
/* Project created methods */
void sys_exit (int);
static pid_t sys_exec (const char *);
static bool sys_create (const char *, uint32_t);
static int sys_wait (pid_t);
static bool sys_remove (const char *);
static int sys_open (const char *);
static int sys_filesize (int);
static int sys_read (int, void *, uint32_t);
static int sys_write (int, const void *, uint32_t);
static void sys_seek (int, uint32_t);
static uint32_t sys_tell (int);
static void sys_close (int);
static bool sys_chdir (const char *);
static bool sys_mkdir (const char *);
static bool sys_readdir (int, char *);
static bool sys_isdir (int);
static int sys_inumber (int);
static void valid_ptr (const void*);
static int valid_index (int);

static struct file * open_helper (const char*);
static off_t filesize_helper (struct file*);
static off_t read_helper (struct file *, void *, off_t);
static off_t write_helper (struct file *, const void *, off_t);
static void seek_helper (struct file *, off_t);
static off_t tell_helper (struct file *);
static void close_helper (struct file *);

static struct lock fs_lock;

/* Quick helper function to test if a pointer points to user
   memory. If the pointer is bad, exit the user program. */
void
valid_ptr (const void *usrdata)
{
	if (!(usrdata && is_user_vaddr (usrdata) &&
  pagedir_get_page (thread_current ()->pagedir, usrdata)))
    sys_exit (-1);
}

/* Initialize the system call handler and the file system lock. */
void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init (&fs_lock);
}

/* Syscall_handler will get the interrupt value, and
   based on the switch case, call the appropriate system call
   function. */
static void
syscall_handler (struct intr_frame *f UNUSED)
{
  int sys_call_num;
  void *default_esp = f->esp;
  valid_ptr (f->esp);


  f->esp = pop (f->esp, (void *) &sys_call_num, sizeof (int));
  valid_ptr (f->esp);

  switch (sys_call_num)
  {
    case SYS_HALT:
      {
        shutdown_power_off ();
        break;
      }

    case SYS_EXIT:
      { 
        int arg0;
        f->esp = pop (f->esp, (void *) &arg0, sizeof (int));
        valid_ptr (f->esp);
        sys_exit (arg0);
        break;
      }

    case SYS_EXEC:
      {
        char *arg0;
        f->esp = pop (f->esp, (void *) &arg0, sizeof (char *));
        valid_ptr (f->esp);
        valid_ptr (arg0);
        f->eax = sys_exec (arg0);
        break;
      }

    case SYS_WAIT:
      { 
        pid_t arg0;
        f->esp = pop (f->esp, (void *) &arg0, sizeof (pid_t));
        valid_ptr (f->esp);
        f->eax = sys_wait (arg0);
        break;
      }

    case SYS_CREATE:
      {
        char *arg0;
        uint32_t arg1;
        f->esp = pop (f->esp, (void *) &arg0, sizeof (char *));
        valid_ptr (f->esp);
        f->esp = pop (f->esp, (void *) &arg1, sizeof (uint32_t));
        valid_ptr (f->esp);
        valid_ptr (arg0);
        f->eax = sys_create (arg0, arg1);
        break;
      }

    case SYS_REMOVE:
      {
        char *arg0;
        f->esp = pop (f->esp, (void *) &arg0, sizeof (char *));
        valid_ptr (f->esp);
        valid_ptr (arg0);
        f->eax = sys_remove (arg0);
        break;
      }

    case SYS_OPEN:
      { 
        char *arg0;
        f->esp = pop (f->esp, (void *) &arg0, sizeof (char *));
        valid_ptr (f->esp);
        valid_ptr (arg0);
        f->eax = sys_open (arg0);
        break;
      }

    case SYS_FILESIZE:
      { 
        int arg0;
        f->esp = pop (f->esp, (void *) &arg0, sizeof (int));
        valid_ptr (f->esp);
        f->eax = sys_filesize (arg0);
        break;
      }

    case SYS_READ:
      {
        int arg0;
        void *arg1;
        uint32_t arg2;
        f->esp = pop (f->esp, (void *) &arg0, sizeof (int));
        valid_ptr (f->esp);
        f->esp = pop (f->esp, (void *) &arg1, sizeof (void *));
        valid_ptr (f->esp);
        f->esp = pop (f->esp, (void *) &arg2, sizeof (uint32_t));
        valid_ptr (f->esp);
        valid_ptr (arg1);
        f->eax = sys_read (arg0, arg1, arg2);
        break;
      }

    case SYS_WRITE:
      {
        int arg0;
        void *arg1;
        uint32_t arg2;
        f->esp = pop (f->esp, (void *)&arg0, sizeof (int));
        valid_ptr (f->esp);
        f->esp = pop (f->esp, (void *)&arg1, sizeof (void *));
        valid_ptr (f->esp);
        f->esp = pop (f->esp, (void *)&arg2, sizeof (uint32_t));
        valid_ptr (f->esp);
        valid_ptr (arg1);
        f->eax = sys_write (arg0, arg1, arg2);
        break;
      }

    case SYS_SEEK:
      {
        int arg0;
        uint32_t arg1;
        f->esp = pop (f->esp, (void *) &arg0, sizeof (int));
        valid_ptr (f->esp);
        f->esp = pop (f->esp, (void *) &arg1, sizeof (uint32_t));
        valid_ptr (f->esp);
        sys_seek (arg0, arg1);
        break;
      }

    case SYS_TELL:
      { 
        int arg0;
        f->esp = pop (f->esp, (void *) &arg0, sizeof (int));
        valid_ptr (f->esp);
        f->eax = sys_tell (arg0);
        break;
      }

    case SYS_CLOSE:
      {
        int arg0;
        f->esp = pop (f->esp, (void *) &arg0, sizeof (int));
        valid_ptr (f->esp);
        sys_close (arg0);
        break;
      }

    case SYS_CHDIR: 
      {
        const char *arg0;
        f->esp = pop (f->esp, (void *) &arg0, sizeof (int));
        valid_ptr (f->esp);
        f->eax = sys_chdir (arg0);
        break;
      }

    case SYS_MKDIR: 
      {
        const char *arg0;
        f->esp = pop (f->esp, (void *) &arg0, sizeof (int));
        valid_ptr (f->esp);
        f->eax = sys_mkdir (arg0);
        break;
      }

    case SYS_READDIR:
      {
        int arg0;
        char *arg1;
        f->esp = pop (f->esp, (void *) &arg0, sizeof (int));
        valid_ptr (f->esp);
        f->esp = pop (f->esp, (void *) &arg1, sizeof (uint32_t));
        valid_ptr (f->esp);
        f->eax = sys_readdir (arg0, arg1);
        break;
      }

    case SYS_ISDIR: 
      {
        int arg0;
        f->esp = pop (f->esp, (void *) &arg0, sizeof (int));
        valid_ptr (f->esp);
        f->eax = sys_isdir (arg0);
        break;
      }

    case SYS_INUMBER: 
      {
        int arg0;
        f->esp = pop (f->esp, (void *) &arg0, sizeof (int));
        valid_ptr (f->esp);
        f->eax = sys_inumber (arg0);
        break;
      }
  }
  f->esp = default_esp;
}

/* When the user process attempts to exit, set the thread
   exit status accordingly. We also have a function, set_orphan,
   available to then mark all of the exiting process's children
   as orphans. Furthermore, we take care to close every open
   file that an exiting process has opened. */
void
sys_exit (int status)
{
  lock_acquire (&fs_lock);
  struct thread *t;
  t = thread_current ();
  t->exit_status = status;
  int i;

  for (i = 0; i < FDMAX; i++)
    if (t->fds[i])
      file_close (t->fds[i]);

  set_orphan (t);

  char output[32];
  int num_bytes = snprintf (output, 32, "%s: exit(%d)\n",
    t->name, t->exit_status);
  putbuf (output, num_bytes);
  file_close (t->self_executable);
  // dir_close (t->cur_dir);
  lock_release (&fs_lock);
  thread_exit ();
}

/* Call for user process to exec, which works similar
   to the UNIX fork(). */
pid_t
sys_exec (const char *cmdline)
{
  return process_execute (cmdline);
}

/* Call for the user process to wait on a child process.
   No other kinds of processes can be waited upon, and
   no child process can be waited upon more than once.
   We return -1 immediately if we cannot wait on the
   child process. If the child process has already terminated,
   this function will immediately return the exit status of
   the child process. */
int
sys_wait (pid_t pid)
{
  return process_wait (pid);
}

/* Create a file in the file system. All system
   calls which use the file system are considered
   atomic operations. */
bool
sys_create (const char *file, uint32_t initial_size)
{
  lock_acquire (&fs_lock);
  bool create_return = filesys_create (file, (off_t) initial_size);
  lock_release (&fs_lock);
  return create_return;
}

/* If a file is open when it is removed, its blocks are
   not deallocated and it may still be accessed by any
   threads that have it open, until the last one closes it. */
bool
sys_remove (const char *file)
{
  lock_acquire (&fs_lock);
  bool create_return = filesys_remove (file);
  lock_release (&fs_lock);
  return create_return;
}

/* Open a file for the user process. Add the file to the
   process's file descriptor table. If the FDT is full,
   return -1 (but do not terminate the process). */
int
sys_open (const char *file)
{
  struct file *f;
  struct thread *t;
  int i;
  t = thread_current ();

  if (t->fd_size >= FDMAX)
    return -1;

  f = open_helper (file);
  if (!f)
    return -1;

  for (i = 0; i < FDMAX && t->fds[i]; i++);
  t->fds[i] = f;
  t->fd_size++;
  return i+2;
}

/* Helper function to open files atomically. */
struct file *
open_helper (const char *name)
{
  struct file *f;
  lock_acquire (&fs_lock);
  f = filesys_open (name);
  lock_release (&fs_lock);
  return f;
}

/* Return the file size. Canot use STDIN/STDOUT. */
int
sys_filesize (int fd)
{
  struct thread *t;
  struct file *f;
  int retval;
  t = thread_current ();
  retval = -1;
  fd = valid_index (fd);
  if (fd < 0)
    sys_exit (-1);

  f = t->fds[fd];
  if (f)
    retval = (int) filesize_helper (t->fds[fd]);
  else
    sys_exit (-1);
  return retval;
}

/* Helper function to open files atomically. */
off_t
filesize_helper (struct file *f)
{
  lock_acquire (&fs_lock);
  off_t size;
  size = file_length (f);
  lock_release (&fs_lock);
  return size;
}

/* Reads size bytes from the file into buffer. We
always read as much as was requested, and we do not
read from STDOUT. If STDIN, read from the keyboard. */
int
sys_read (int fd, void *buffer, unsigned size)
{
	struct thread *t;
	t = thread_current ();
	int bytes_read = 0;
	// char string_from_key;
	if (fd == 0)
		{
      char *strbuf = buffer;
      while (bytes_read < size)
      	strbuf[bytes_read++] = input_getc ();
		}
    else if (fd == 1)
      sys_exit (-1);
    else if ((fd = valid_index (fd)) >= 0)
      bytes_read = (int) read_helper (t->fds[fd], buffer, size);
    else
    {
      sys_exit (-1);
      return -1;
    }
  return bytes_read;
}

/* Helper function to open files atomically. */
off_t
read_helper (struct file *file, void *buffer, off_t size)
{
  lock_acquire (&fs_lock);
  off_t read_size;
  read_size = file_read (file, buffer, size);
  lock_release (&fs_lock);
  return read_size;
}

/* Write from buffer into the file described by fd, or
   write out to STDOUT (the console). If we write to
   STDOUT, we first break up the output into 256 byte
   sized chunks. */
int
sys_write (int fd, const void *buffer, uint32_t size)
{
  struct thread *t = thread_current ();
  struct file *f;
  struct inode *inode;
  int bytes_out = 0;

  if (fd == 1)
  {
    while ((int) size > bytes_out + 256)
    {
      putbuf (buffer, (size_t) 256);
      bytes_out += 256;
    }
    putbuf (buffer, (size_t) (size - bytes_out));
    bytes_out = size;
  }
  else if (fd == 0)
    sys_exit (-1);
  else
  {
    fd = valid_index (fd);
    if (fd < 0)
      sys_exit (-1);
    f = t->fds[fd];
    if (f)
    {
      inode = file_get_inode (f); 
      if (inode_is_dir (inode))
        return -1; 
      bytes_out = write_helper (f, buffer, (off_t) size);
      return bytes_out;
    }
    else
    {
      sys_exit (-1);
      return -1;
    }
  }
  return bytes_out;
}

/* Helper function to open files atomically. */
off_t
write_helper (struct file *file, const void *buffer, off_t size)
{
  lock_acquire (&fs_lock);
  off_t bytes_out;
  bytes_out = file_write (file, buffer, size);
  lock_release (&fs_lock);
  return bytes_out;
}

/* Seek in a file until position. */
void
sys_seek (int fd, uint32_t position)
{
  struct thread *t;
  struct file *f;
  t = thread_current ();
  fd = valid_index (fd);
  if (fd < 0)
    sys_exit (-1);
  f = t->fds[fd];
  if (f)
    seek_helper (f, position);
  else
    sys_exit (-1);
}

/* Helper function to open files atomically. */
void
seek_helper (struct file *file, off_t new_pos)
{
  lock_acquire (&fs_lock);
  file_seek (file, new_pos);
  lock_release (&fs_lock);
}

/* Returns the position of the next call to be written or
   read from in bytes from the beginning of the file. */
uint32_t
sys_tell (int fd)
{
  struct thread *t;
  struct file *f;
  t = thread_current ();
  fd = valid_index (fd);
  if (fd < 0)
  {
    sys_exit (-1);
    return (uint32_t) -1;
  }
  f = t->fds[fd];
  if (f)
    return (uint32_t) tell_helper (f);
  else
  {
    sys_exit (-1);
    return -1;
  }
}

/* Helper function to open files atomically. */
off_t
tell_helper (struct file *file)
{
  lock_acquire (&fs_lock);
  off_t pos;
  pos = file_tell (file);
  lock_release (&fs_lock);
  return pos;
}

/* Close a file that has been opened by the user process.
   If the file doesn't exist, exit user program. */
void
sys_close (int fd)
{
  struct thread *t = thread_current ();

  // if fd-2 is a valid index and the file descriptor corresponds
  // to an open file
  if (fd > 1 && (fd-2) < (int) FDMAX && t->fds[fd-2])
  {
    close_helper (t->fds[fd-2]);
    t->fds[fd-2] = NULL;
    t->fd_size--;
  }
  else
    sys_exit(-1);
}

/* Helper function to open files atomically. */
void
close_helper (struct file *file)
{
  lock_acquire (&fs_lock);
  file_close (file);
  lock_release (&fs_lock);
}

/* Change the current directory. */
// Check for abs/rel paths
bool
sys_chdir (const char *dir)
{
  struct thread *t = thread_current (); 
  struct inode *inode;
  char filename[NAME_MAX + 1];
  struct dir *parent_new = filesys_pathfinder (dir, filename);

  if (parent_new == NULL || !dir_lookup (parent_new, filename, &inode))
    {
      return false;
    }
  struct dir *new = dir_open (inode);
  if (new == NULL)
    return false;
  dir_close (t->cur_dir);
  t->cur_dir = new;
  return true;
}

/* Create a directory. */
bool
sys_mkdir (const char *dir)
{
  return dir_mkdir (dir); 
}

/* Reads a directory entry. */
bool
sys_readdir (int fd, char *name)
{
  struct thread *t = thread_current ();
  struct file *f;
  fd = valid_index (fd);
  if (fd < 0)
    {
      sys_exit (-1);
      return -1;  // gets rid of warning
    }
  f = t->fds[fd];
  if (f && file_isdir (f))
    {
      struct dir *readdir = dir_open (file_get_inode (f));
      /* Set pos */
      dir_setpos(readdir, file_tell(f));
      if (readdir == NULL)
        {
          sys_exit (-1);
          return -1;  // gets rid of warning
        }
      bool retval = dir_readdir(readdir, name);
      file_seek(f, dir_getpos(readdir));
      return retval;
    }
  sys_exit (-1);
  return false;
}

/* Tests if a fd represents a directory. */
bool
sys_isdir (int fd)
{
  struct thread *t = thread_current ();
  struct file *f;
  fd = valid_index (fd);
  if (fd < 0)
    {
      sys_exit (-1);
      return -1;  // gets rid of warning
    }
  f = t->fds[fd];
  if (f)
    return file_isdir (f);
  sys_exit (-1);
  return false;  // gets rid of warning
}

/* Returns the inode number for a fd. */
int
sys_inumber (int fd)
{
  struct thread *t = thread_current ();
  struct file *f;
  fd = valid_index (fd); 
  if (fd < 0)
    {
      sys_exit (-1);
      return -1;  // gets rid of warning
    }
  f = t->fds[fd];
  if (f)
    {
      struct inode *ino = file_get_inode (f);
      return inode_get_inumber (ino);
    }
  sys_exit (-1);
  return -1;  // gets rid of warning
}

/* Valid_index is a helper function designed to
   match the file descriptor to its index in the
   FDT. This is necessary because fd = 0, 1 are
   both reserved for STDIN, STDOUT, respectively. */
int
valid_index (int fd)
{
  fd -= 2;
  if ( fd < FDMAX && fd >= 0 )
    return fd;
  else
    return -1;
}
