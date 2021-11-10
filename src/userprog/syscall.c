#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/vaddr.h" // is_user_addr()
#include "userprog/pagedir.h" // pagedir_get_page()
#include "devices/shutdown.h" // shutdown_power_off()

#define STDOUT 1
#define ERR -1


static void syscall_handler (struct intr_frame *);

static void syscall_halt (void);
static void syscall_exit (int status);
static int syscall_write(int fd, const void *buffer, unsigned size);
static int
syscall_open (const char *file);

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
    syscall_exit (ERR);
}

static void
check_memory (const void *begin_addr, int size)
{
  /* Check the entire memory slice accessed through this pointer */
  for (const void *ptr = begin_addr; ptr < begin_addr + size; ptr += PGSIZE)
    {
      if (!is_valid_addr (ptr))
        syscall_exit (ERR);
    }

  if (!is_valid_addr (begin_addr + sizeof (void *) - 1))
    syscall_exit (ERR);
}

static void
syscall_handler (struct intr_frame *f UNUSED)
{
  /* First check if f->esp is a valid pointer */
  is_valid_ptr (f->esp, 0);

  switch (*(int *) f->esp)
    {
    case SYS_HALT:
      syscall_halt();
      break;
    case SYS_EXIT:
      is_valid_ptr (f->esp, 1);
      int status = *((int *) f->esp + 1);
      syscall_exit (status);
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
      printf ("syscall create.\n");
      break;
    case SYS_REMOVE:
      is_valid_ptr (f->esp, 1);
      printf ("syscall remove.\n");
      break;
    case SYS_OPEN:
      is_valid_ptr (f->esp, 1);
      //printf ("syscall open.\n");
      //printf ("\n SYS_OPEN: %s \n", *((char **) f->esp + 1));
      f->eax = syscall_open (*((char **) f->esp + 1));
      break;
    case SYS_FILESIZE:
      is_valid_ptr (f->esp, 1);
      printf ("syscall filesize.\n");
      break;
    case SYS_READ:
      is_valid_ptr (f->esp, 3);
      printf ("syscall read.\n");
      break;
    case SYS_WRITE:
      is_valid_ptr (f->esp, 3);
      int fd = *((int *) f->esp + 1);
      void *buffer = (void *) (*((int *) f->esp + 2));
      unsigned size = *((unsigned *) f->esp + 3);
      check_memory (buffer, size);

      /* Place return value in EAX register */
      f->eax = syscall_write (fd, buffer, size);
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
      printf ("syscall close.\n");
      break;
    default:
      printf ("unknown syscall.\n");
    }
}

static void
syscall_halt (void)
{
  shutdown_power_off();
}

static void
syscall_exit (int status)
{
  thread_current ()->exit_code = status;
  thread_exit ();
}

static int
syscall_write (int fd, const void *buffer, unsigned size)
{
  if (fd == STDOUT)
    {
      putbuf (buffer, size);
      return size;
    }
  else
    return -1;
}

static int
syscall_open (const char *file)
{


  //printf ("\n yo \n");
  if (!file)
    return -1;
  //printf ("file: %s\n", file);
  // TODO: Think about the lock
  // lock_acquire (&filesys_lock);
  struct file *f = filesys_open (file);
  // lock_release (&filesys_lock);

  if (f == NULL)
    return -1;

  struct thread *t = thread_current ();
  struct file_descriptor *fd = malloc (sizeof (struct file_descriptor));
  fd->file = f;
  fd->fd = t->next_fd++;
  list_push_back (&t->fd_list, &fd->elem);

  return fd->fd;
}