#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  /*printf ("system call!\n");
  thread_exit ();*/
  if (!f->esp)
    thread_exit ();

  switch(*(int*)f->esp)
  {
    case SYS_HALT:
      printf("syscall halt.\n");
      break;
    case SYS_EXIT:
      printf("syscall exit.\n");
      break;
    case SYS_EXEC:
      printf("syscall exec.\n");
      break;
    case SYS_WAIT:
      printf("syscall wait.\n");
      break;
    case SYS_CREATE:
      printf("syscall create.\n");
      break;
    case SYS_REMOVE:
      printf("syscall remove.\n");
      break;
    case SYS_OPEN:
      printf("syscall open.\n");
      break;
    case SYS_FILESIZE:
      printf("syscall filesize.\n");
      break;
    case SYS_READ:
      printf("syscall read.\n");
      break;
    case SYS_WRITE:
      printf("syscall write.\n");
      break;
    case SYS_SEEK:
      printf("syscall seek.\n");
      break;
    case SYS_TELL:
      printf("syscall tell.\n");
      break;
    case SYS_CLOSE:
      printf("syscall close.\n");
      break;
    default:
      printf("unknown syscall.\n");
  }
}
