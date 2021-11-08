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
  // int *esp = f->esp;
  if (!f->esp)
    thread_exit ();
  // printf ("esp: %d", (int *) f->esp);
  switch (*(int *) f->esp)
    {
    case SYS_HALT:
      printf ("syscall halt.\n");
      break;
    case SYS_EXIT:
      {
        // printf ("syscall SYS_EXIT.\n");
        int status = *((int *) f->esp + 1);
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
        // printf ("syscall WRITE.\n");
        int fd = *((int *) f->esp + 1); // casting for pointer arithmetic

        void *buffer = (void *) (*((int *) f->esp + 2)); // dereference before casting to get the contents
        unsigned size = *((unsigned *) f->esp + 3);

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
  thread_current ()->exit_code = status; // set exit status
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
