#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "stdbool.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/synch.h"

#include <string.h>

#include "threads/vaddr.h"    // is_user_addr()
#include "userprog/pagedir.h" // pagedir_get_page()
#include "devices/shutdown.h" // shutdown_power_off()
#include "filesys/filesys.h"  // filesys_ functions

#define STDIN 0
#define STDOUT 1
#define ERR -1


static void syscall_handler (struct intr_frame *);


static int
syscall_open (const char *file);
static int
syscall_close (int fd);
static int
stscall_read (int fd, void *buffer, unsigned size);
static void halt (void);
static void exit (int status);
static bool create (const char *file, unsigned initial_size);
static bool remove (const char *file);
static int write (int fd, const void *buffer, unsigned size);
static int
syscall_filesize (int fd);

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
    exit (ERR);
}

static void
check_memory (const void *begin_addr, const int size)
{
  /* Check the entire memory slice accessed through this pointer */
  for (const void *ptr = begin_addr; ptr < begin_addr + size; ptr += PGSIZE)
    {
      if (!is_valid_addr (ptr))
        exit (ERR);
    }

  if (!is_valid_addr (begin_addr + sizeof (void *) - 1))
    exit (ERR);
}

static void
check_string (const char *str)
{
  if (!is_valid_addr (str))
    exit (ERR);

  for (const char *c = str; *c != '\0'; c++)
    {
      if (c - str + 2 >= PGSIZE || !is_valid_addr (c))
        exit (ERR);
    }
}

static void
syscall_handler (struct intr_frame *f UNUSED)
{
  /* First check if f->esp is a valid pointer */
  is_valid_ptr (f->esp, 0);

  switch (*(int *) f->esp)
    {
    case SYS_HALT:
      halt ();
      break;
    case SYS_EXIT:
      is_valid_ptr (f->esp, 1);
      int status = *((int *) f->esp + 1);
      exit (status);
      break;
    case SYS_EXEC:
      is_valid_ptr (f->esp, 1);
      printf ("syscall exec.\n");
      break;
    case SYS_WAIT:
      is_valid_ptr (f->esp, 1);
      printf ("syscall wait.\n");
      break;
    case SYS_CREATE:
      is_valid_ptr (f->esp, 2);
      const char *file_cr = (const char *) (*((int *) f->esp + 1));
      unsigned initial_size = *((unsigned *) f->esp + 2);
      check_string (file_cr);

      lock_acquire (&file_lock);
      f->eax = create (file_cr, initial_size);
      lock_release (&file_lock);
      break;
    case SYS_REMOVE:
      is_valid_ptr (f->esp, 1);
      const char *file_rm = (const char *) (*((int *) f->esp + 1));
      check_string (file_rm);

      lock_acquire (&file_lock);
      f->eax = remove (file_rm);
      lock_release (&file_lock);
      break;
    case SYS_OPEN:

      is_valid_ptr (f->esp, 1);
      //printf ("syscall open.\n");
      //printf ("\n SYS_OPEN: %s \n", *((char **) f->esp + 1));
      f->eax = syscall_open (*((char **) f->esp + 1));
      break;
    case SYS_FILESIZE:
      is_valid_ptr (f->esp, 1);
      //printf ("syscall filesize.\n");
      f->eax = syscall_filesize (*((int *) f->esp + 1));
      break;
    case SYS_READ:
      is_valid_ptr (f->esp, 3);
      //printf ("syscall read.\n");
      // int fd = *((int *) f->esp + 1);
      // void *buffer = (void *) (*((int *) f->esp + 2));
      // unsigned size = *((unsigned *) f->esp + 3);
      check_memory (*((void **) f->esp + 2), *((unsigned *) f->esp + 3));
      f->eax = stscall_read (*((int *) f->esp + 1), (void *) (*((int *) f->esp + 2)), *((unsigned *) f->esp + 3));

      //f->eax = stscall_read (fd, buffer, size);
      break;
    case SYS_WRITE:
      is_valid_ptr (f->esp, 3);
      int fd = *((int *) f->esp + 1);
      void *buffer = (void *) (*((int *) f->esp + 2));
      unsigned size = *((unsigned *) f->esp + 3);
      check_memory (buffer, size);
      f->eax = write (fd, buffer, size);
      break;
    case SYS_SEEK:
      is_valid_ptr (f->esp, 2);
      printf ("syscall seek.\n");
      break;
    case SYS_TELL:
      is_valid_ptr (f->esp, 1);
      printf ("syscall tell.\n");
      break;
    case SYS_CLOSE:
      is_valid_ptr (f->esp, 1);
      //printf ("syscall close.\n");
      f->eax = syscall_close (*((int **) f->esp + 1));
      break;
    default:
      printf ("unknown syscall.\n");
    }
}

static void
halt (void)
{
  shutdown_power_off ();
}

static void
exit (int status)
{
  thread_current ()->exit_code = status;
  thread_exit ();
}

static bool
create (const char *file, unsigned initial_size)
{
  if (strlen (file) == 0)
    return false;
  return filesys_create (file, initial_size);
}

static bool
remove (const char *file)
{
  return filesys_remove (file);
}

static int
write (int fd, const void *buffer, unsigned size)
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
      struct thread *t = thread_current ();
      for (struct list_elem *e = list_begin (&t->fd_list); e != list_end (&t->fd_list); e = list_next (e))
        {
          struct file_descriptor *f = list_entry (e, struct file_descriptor, elem);
          if (f == NULL)
            {
              lock_release (&file_lock);
              return -1;
            }
          if (f->fd == fd)
            {
              // printf ("\n buffer: %p \n", buffer);
              // printf ("\n buffer: %s \n", buffer);
              // printf ("\n size: %ud \n", size);
              int ret = file_write (f->file, buffer, size);
              lock_release (&file_lock);
              return ret;
            }
        }
    }
  lock_release (&file_lock);
  return -1;
}

static int
syscall_open (const char *file)
{
  if (file == NULL)
    return -1;

  char *p = file;
  while (pagedir_get_page ((void *) thread_current ()->pagedir, p) != NULL && *p != '\0')
    p++;
  if (pagedir_get_page ((void *) thread_current ()->pagedir, p) == NULL)
    {
      exit (ERR); //TODO: pass open-bad-ptr but may have side effects
      return -1;
    }


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
      if (f == NULL || f->fd == NULL)
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
  //lock_release (&file_lock);
  lock_release (&file_lock);
  return -1;
}

static int
stscall_read (int fd, void *buffer, unsigned size)
{
  if (fd == STDIN)
    {
      for (int i = 0; i < size; i++)
        {
          *((char *) buffer + i) = input_getc ();
        }
      return size;
    }
  else
    {
      struct thread *t = thread_current ();
      lock_acquire (&file_lock);
      for (struct list_elem *e = list_begin (&t->fd_list); e != list_end (&t->fd_list); e = list_next (e))
        {
          struct file_descriptor *f = list_entry (e, struct file_descriptor, elem);
          if (f == NULL || f->fd == NULL)
            {
              lock_release (&file_lock);
              return -1;
            }
          if (f->fd == fd)
            {
              int bytes_read = file_read (f->file, buffer, size);
              lock_release (&file_lock);
              // printf ("return: %d", bytes_read);
              return bytes_read;
            }
        }
      lock_release (&file_lock);
      return -1;
    }
}

static int
syscall_filesize (int fd)
{
  struct thread *t = thread_current ();
  lock_acquire (&file_lock);
  for (struct list_elem *e = list_begin (&t->fd_list); e != list_end (&t->fd_list); e = list_next (e))
    {
      struct file_descriptor *f = list_entry (e, struct file_descriptor, elem);
      if (f == NULL || f->fd == NULL)
        {
          lock_release (&file_lock);
          return -1;
        }
      if (f->fd == fd)
        {
          int size = file_length (f->file);
          lock_release (&file_lock);
          return size;
        }
    }
  lock_release (&file_lock);
  return -1;
}