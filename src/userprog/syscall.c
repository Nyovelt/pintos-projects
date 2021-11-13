#include "userprog/syscall.h"
#include <syscall-nr.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "userprog/pagedir.h"
#include "devices/shutdown.h"
#include "devices/input.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "userprog/process.h"

typedef int pid_t;
#define STDIN 0
#define STDOUT 1

static void
syscall_handler (struct intr_frame *);

static int syscall_open (const char *file);
static int syscall_close (int fd);
static int syscall_read (int fd, void *buffer, unsigned size);
static void syscall_halt (void);
static void syscall_exit (int status);
static bool syscall_create (const char *file, unsigned initial_size);
static bool syscall_remove (const char *file);
static int syscall_write (int fd, const void *buffer, unsigned size);
static int syscall_filesize (int fd);
static void syscall_seek (int fd, unsigned position);
static unsigned syscall_tell (int fd);
static pid_t syscall_exec (const char *cmd_line);
static int syscall_wait (pid_t pid);

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static inline bool
is_valid_addr (const void *addr)
{
  return is_user_vaddr (addr) && pagedir_get_page (thread_current ()->pagedir, addr);
}

static inline void
is_valid_ptr (const void *esp, const int offset)
{
  const void *ptr = esp + offset * sizeof (void *);
  /* Check the first and last bytes */
  if (!is_valid_addr (ptr) || !is_valid_addr (ptr + sizeof (void *) - 1))
    syscall_exit (-1);
}

/* Check the head, tail and pages */
static void
check_memory (const void *begin_addr, const int size)
{
  /* Check the entire memory slice accessed through this pointer */
  for (const void *ptr = begin_addr; ptr < begin_addr + size; ptr += PGSIZE)
    {
      if (!is_valid_addr (ptr))
        syscall_exit (-1);
    }

  if (!is_valid_addr (begin_addr + sizeof (void *) - 1))
    syscall_exit (-1);
}

static void
check_string (const char *str)
{
  if (!is_valid_addr (str))
    syscall_exit (-1);

  for (const char *c = str; *c != '\0';)
    {
      if (!is_valid_addr (++c))
        syscall_exit (-1);
    }
}

static struct file_descriptor *
get_file_descriptor (int fd)
{
  struct thread *t = thread_current ();
  if (list_begin (&t->fd_list) == NULL)
    return NULL;
  for (struct list_elem *e = list_begin (&t->fd_list); e != list_end (&t->fd_list); e = list_next (e))
    {
      struct file_descriptor *f = list_entry (e, struct file_descriptor, elem);
      if (f == NULL || f->fd == 0)
        return NULL;
      if (f->fd == fd)
        return f;
    }
  return NULL;
}

static void
syscall_handler (struct intr_frame *f UNUSED)
{
  /* First check if f->esp is a valid pointer */
  is_valid_ptr (f->esp, 0);

  switch (*(int *) f->esp)
    {
    case SYS_HALT:
      syscall_halt ();
      break;
    case SYS_EXIT:
      is_valid_ptr (f->esp, 1);
      syscall_exit (*((int *) f->esp + 1));
      break;
    case SYS_EXEC:
      is_valid_ptr (f->esp, 1);
      f->eax = syscall_exec (*((char **) f->esp + 1));
      break;
    case SYS_WAIT:
      is_valid_ptr (f->esp, 1);
      f->eax = syscall_wait (*((pid_t *) f->esp + 1));
      break;
    case SYS_CREATE:
      is_valid_ptr (f->esp, 2);
      check_string (*((const char **) f->esp + 1));
      f->eax = syscall_create (*((const char **) f->esp + 1), *((unsigned *) f->esp + 2));
      break;
    case SYS_REMOVE:
      is_valid_ptr (f->esp, 1);
      check_string (*((const char **) f->esp + 1));
      f->eax = syscall_remove (*((const char **) f->esp + 1));
      break;
    case SYS_OPEN:
      is_valid_ptr (f->esp, 1);
      f->eax = syscall_open (*((char **) f->esp + 1));
      break;
    case SYS_FILESIZE:
      is_valid_ptr (f->esp, 1);
      f->eax = syscall_filesize (*((int *) f->esp + 1));
      break;
    case SYS_READ:
      is_valid_ptr (f->esp, 3);
      check_memory (*((void **) f->esp + 2), *((unsigned *) f->esp + 3));
      f->eax = syscall_read (*((int *) f->esp + 1), (void *) (*((int *) f->esp + 2)), *((unsigned *) f->esp + 3));
      break;
    case SYS_WRITE:
      is_valid_ptr (f->esp, 3);
      check_memory (*((void **) f->esp + 2), *((unsigned *) f->esp + 3));
      f->eax = syscall_write (*((int *) f->esp + 1), (*((void **) f->esp + 2)), *((unsigned *) f->esp + 3));
      break;
    case SYS_SEEK:
      is_valid_ptr (f->esp, 2);
      syscall_seek (*((int *) f->esp + 1), *((unsigned *) f->esp + 2));
      break;
    case SYS_TELL:
      is_valid_ptr (f->esp, 1);
      f->eax = syscall_tell (*((int *) f->esp + 1));
      break;
    case SYS_CLOSE:
      is_valid_ptr (f->esp, 1);
      f->eax = syscall_close (*((int *) f->esp + 1));
      break;
    default:
      printf ("unknown syscall.\n");
    }
}

static void
syscall_halt (void)
{
  shutdown_power_off ();
}

static void
syscall_exit (int status)
{
  thread_current ()->exit_code = status;
  thread_exit ();
}

static bool
syscall_create (const char *file, unsigned initial_size)
{
  if (strlen (file) == 0)
    return false;
  lock_acquire (&file_lock);
  bool ret = filesys_create (file, initial_size);
  lock_release (&file_lock);
  return ret;
}

static bool
syscall_remove (const char *file)
{
  lock_acquire (&file_lock);
  bool ret = filesys_remove (file);
  lock_release (&file_lock);
  return ret;
}

static int
syscall_write (int fd, const void *buffer, unsigned size)
{
  lock_acquire (&file_lock);
  if (fd == STDOUT_FILENO)
    {
      putbuf (buffer, size);
      lock_release (&file_lock);
      return size;
    }
  else if (fd == STDIN_FILENO)
    {
      lock_release (&file_lock);
      return -1;
    }
  else
    {
      struct file_descriptor *f = get_file_descriptor (fd);
      if (f == NULL || f->fd == 0 || f->file == NULL)
        {
          lock_release (&file_lock);
          return -1;
        }
      int ret = file_write (f->file, buffer, size);
      lock_release (&file_lock);
      return ret;
    }
  lock_release (&file_lock);
  return -1;
}

static int
syscall_open (const char *file)
{
  check_string (file);

  lock_acquire (&file_lock);
  struct file *f = filesys_open (file);
  lock_release (&file_lock);

  if (f == NULL)
    return -1;

  struct thread *t = thread_current ();
  struct file_descriptor *fd = malloc (sizeof (struct file_descriptor));
  fd->file = f;
  fd->fd = t->next_fd++;
  list_push_back (&t->fd_list, &fd->elem);

  return fd->fd;
}

static int
syscall_close (int fd)
{
  struct thread *t = thread_current ();
  lock_acquire (&file_lock);
  for (struct list_elem *e = list_begin (&t->fd_list); e != list_end (&t->fd_list); e = list_next (e))
    {
      struct file_descriptor *f = list_entry (e, struct file_descriptor, elem);
      if (f == NULL || f->fd == 0)
        {
          lock_release (&file_lock);
          return -1;
        }
      if (f->fd == fd)
        {
          list_remove (e);
          file_close (f->file);
          free (f);
          lock_release (&file_lock);
          return 0;
        }
    }
  lock_release (&file_lock);
  return -1;
}

static int
syscall_read (int fd, void *buffer, unsigned size)
{
  lock_acquire (&file_lock);
  if (fd == STDIN)
    {
      for (unsigned int i = 0; i < size; i++)
        *((char *) buffer + i) = input_getc ();
      lock_release (&file_lock);
      return size;
    }
  else if (fd != STDOUT)
    {
      struct file_descriptor *f = get_file_descriptor (fd);
      if (f == NULL || f->fd == 0 || f->file == NULL)
        {
          lock_release (&file_lock);
          return -1;
        }
      int bytes_read = file_read (f->file, buffer, size);
      lock_release (&file_lock);
      return bytes_read;
    }
  lock_release (&file_lock);
  return -1;
}

static int
syscall_filesize (int fd)
{
  lock_acquire (&file_lock);
  struct file_descriptor *f = get_file_descriptor (fd);
  if (f == NULL || f->fd == 0 || f->file == NULL)
    {
      lock_release (&file_lock);
      return -1;
    }
  int size = file_length (f->file);
  lock_release (&file_lock);
  return size;
}

void
syscall_seek (int fd, unsigned position)
{
  lock_acquire (&file_lock);
  struct file_descriptor *f = get_file_descriptor (fd);
  if (f == NULL || f->fd == 0 || f->file == NULL)
    {
      lock_release (&file_lock);
      return;
    }
  file_seek (f->file, position);
  lock_release (&file_lock);
}

static unsigned
syscall_tell (int fd)
{
  lock_acquire (&file_lock);
  struct file_descriptor *f = get_file_descriptor (fd);
  if (f == NULL || f->fd == 0 || f->file == NULL)
    {
      lock_release (&file_lock);
      return -1;
    }
  int pos = file_tell (f->file);
  lock_release (&file_lock);
  return pos;
}

static pid_t
syscall_exec (const char *cmd_line)
{
  check_string (cmd_line);

  pid_t pid = process_execute (cmd_line); // 创建用户进程并获得 tid

  thread_yield ();
  return pid;
}

static int
syscall_wait (pid_t pid)
{
  if (pid == -1)
    return -1;
  return process_wait (pid);
}