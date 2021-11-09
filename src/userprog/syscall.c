#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

#include "threads/vaddr.h" // is_user_addr
#include "userprog/pagedir.h" // pagedir_get_page

#define ARGS_MAX 3

#define STDOUT 1
#define ERR -1

//void *args[ARGS_MAX];

static void syscall_handler (struct intr_frame *);

static void syscall_exit (int status);
static int syscall_write(int fd, const void *buffer, unsigned size);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static inline bool
is_valid_addr (const void *addr)
{
  return addr && is_user_vaddr (addr) && pagedir_get_page (thread_current ()->pagedir, addr);
}

static void
check_memory (const void *base, const int offset)
{
  const void *begin_addr = base + offset * sizeof(void *);
  /* Check the entire memory slice accessed through this pointer */
  for (const void *ptr = begin_addr; ptr < begin_addr + sizeof (void *); ptr += PGSIZE)
    {
      if (!is_valid_addr (ptr))
        syscall_exit(ERR);
    }

  /* Check remaining bytes */
  if (!is_valid_addr (begin_addr + sizeof (void *) - 1))
    syscall_exit(ERR);
}

static void
syscall_handler (struct intr_frame *f UNUSED)
{
  /* First check if f->esp is a valid pointer */
  check_memory(f->esp, 0);

  switch (*(int *) f->esp)
    {
    case SYS_HALT:
      printf ("syscall halt.\n");
      break;
    case SYS_EXIT:
        check_memory(f->esp, 1);
        int status = *((int *) f->esp + 1);
        syscall_exit (status);
        break;
    case SYS_EXEC:
      printf ("syscall exec.\n");
      break;
    case SYS_WAIT:
      check_memory(f->esp, 1);
      printf ("syscall wait.\n");
      break;
    case SYS_CREATE:
      check_memory(f->esp, 2);
      printf ("syscall create.\n");
      break;
    case SYS_REMOVE:
      printf ("syscall remove.\n");
      break;
    case SYS_OPEN:
      printf ("syscall open.\n");
      break;
    case SYS_FILESIZE:
      check_memory(f->esp, 1);
      printf ("syscall filesize.\n");
      break;
    case SYS_READ:
      check_memory(f->esp, 3);
      printf ("syscall read.\n");
      break;
    case SYS_WRITE:
        check_memory(f->esp, 3);
        int fd = *((int *) f->esp + 1);
        void *buffer = (void *) (*((int *) f->esp + 2));
        unsigned size = *((unsigned *) f->esp + 3);

        /* Place return value in EAX register */
        f->eax = syscall_write (fd, buffer, size);
        break;
    case SYS_SEEK:
      check_memory(f->esp, 2);
      printf ("syscall seek.\n");
      break;
    case SYS_TELL:
      check_memory(f->esp, 1);
      printf ("syscall tell.\n");
      break;
    case SYS_CLOSE:
      check_memory(f->esp, 1);
      printf ("syscall close.\n");
      break;
    default:
      printf ("unknown syscall.\n");
    }
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
