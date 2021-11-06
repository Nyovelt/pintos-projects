#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

#define STDOUT 1

static void syscall_handler (struct intr_frame *);

static void syscall_exit (int status);
static int syscall_write(int fd, const void *buffer, unsigned size);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED)
{
  if (!f->esp)
    thread_exit ();

  switch (*(int *) f->esp)
    {
    case SYS_HALT:
      printf ("syscall halt.\n");
      break;
    case SYS_EXIT:
      {
        int status = *(int *) (f->esp + 1);
        syscall_exit (status);
        break;
      }
    case SYS_EXEC:
      printf ("syscall exec.\n");
      break;
    case SYS_WAIT:
      printf ("syscall wait.\n");
      break;
    case SYS_CREATE:
      printf ("syscall create.\n");
      break;
    case SYS_REMOVE:
      printf ("syscall remove.\n");
      break;
    case SYS_OPEN:
      printf ("syscall open.\n");
      break;
    case SYS_FILESIZE:
      printf ("syscall filesize.\n");
      break;
    case SYS_READ:
      printf ("syscall read.\n");
      break;
    case SYS_WRITE:
      {
        /* Casting for pointer arithmetic */
        int fd = *(int *) (f->esp + 1);
        /* Dereference before casting to get the contents */
        void *buffer = (void *) (*((int *) f->esp + 2));
        unsigned size = *(unsigned *) (f->esp + 3);

        /* Place return value in EAX register */
        f->eax = syscall_write (fd, buffer, size);
        break;
      }
    case SYS_SEEK:
      printf ("syscall seek.\n");
      break;
    case SYS_TELL:
      printf ("syscall tell.\n");
      break;
    case SYS_CLOSE:
      printf ("syscall close.\n");
      break;
    default:
      printf ("unknown syscall.\n");
    }
}

static void
syscall_exit (int status)
{
  thread_current()->exit_code = status;
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
