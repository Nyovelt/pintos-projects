#include "userprog/syscall.h"
#include <syscall-nr.h>
#include "lib/user/syscall.h"
#include "userprog/pagedir.h"
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
#include "vm/page.h"
#include "vm/frame.h"

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
mapid_t mmap (int fd, void *addr);
static mapid_t syscall_mmap (int fd, const void *addr);
static void syscall_munmap (mapid_t mapid);
static struct mmap_descriptor *get_mmap_descriptor (mapid_t id);
void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static bool
is_valid_addr (const void *addr, bool write)
{
#ifdef VM
  if (!is_user_vaddr (addr))
    return false;

  if (pagedir_get_page (thread_current ()->pagedir, addr) == NULL)
    {
      //printf("page fault for read\n");
      return page_fault_handler(&thread_current()->sup_page_table, addr, write, thread_current()->esp);
    }
  else
    {
      //printf("existing page: %p\n", addr);
      struct sup_page_table_entry *spte = page_lookup(&thread_current()->sup_page_table, addr);
      if (spte != NULL)
        return !(!spte->writable && write);
      else
        return true;
    }
#else
  return is_user_vaddr (addr) && pagedir_get_page (thread_current ()->pagedir, addr);
#endif
}

static void
is_valid_ptr (const void *esp, const int offset, bool write)
{
  //printf("is_valid_ptr: %p\n", esp + offset);
  const void *ptr = esp + offset * sizeof (void *);
  /* Check the first and last bytes */
  if (!is_valid_addr (ptr, write) || !is_valid_addr (ptr + sizeof (void *) - 1, write))
    syscall_exit (-1);
  //printf("valid ptr\n");
}

/* Check the head, tail and pages */
static void
check_memory (const void *begin_addr, const int size, bool write)
{
  //printf("check memory, size: %d\n", size);
  //printf("check end: %p\n", begin_addr + sizeof (void *) - 1);
  if (!is_valid_addr (begin_addr + size - 1, write))
    syscall_exit (-1);

  /* Check the entire memory slice accessed through this pointer */
  for (const void *ptr = begin_addr; ptr < begin_addr + size; ptr += PGSIZE)
    {
      //printf("check: %p\n", ptr);
      if (!is_valid_addr (ptr, write))
        syscall_exit (-1);
      //printf("valid: %p\n", ptr);
    }
  //printf("check memory success\n");
}

static void
check_string (const char *str)
{
  //printf("check string: %s\n", str);
  if (!is_valid_addr (str, false))
    syscall_exit (-1);

  for (const char *c = str; *c != '\0';)
    {
      c++; // avoid deferencing invalid address
      if (c - str + 2 == PGSIZE || !is_valid_addr (c, false))
        syscall_exit (-1);
    }
  //printf("string success\n");
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
  is_valid_ptr (f->esp, 0, false);
#ifdef VM
  thread_current ()->esp = f->esp;
  //printf ("syscall_handler: esp = %p\n", thread_current ()->esp);
#endif

  switch (*(int *) f->esp)
    {
    case SYS_HALT:
      syscall_halt ();
      break;
    case SYS_EXIT:
      is_valid_ptr (f->esp, 1, false);
      syscall_exit (*((int *) f->esp + 1));
      break;
    case SYS_EXEC:
      is_valid_ptr (f->esp, 1, false);
      f->eax = syscall_exec (*((char **) f->esp + 1));
      break;
    case SYS_WAIT:
      is_valid_ptr (f->esp, 1, false);
      f->eax = syscall_wait (*((pid_t *) f->esp + 1));
      break;
    case SYS_CREATE:
      is_valid_ptr (f->esp, 2, false);
      check_string (*((const char **) f->esp + 1));
      f->eax = syscall_create (*((const char **) f->esp + 1), *((unsigned *) f->esp + 2));
      break;
    case SYS_REMOVE:
      is_valid_ptr (f->esp, 1, false);
      check_string (*((const char **) f->esp + 1));
      f->eax = syscall_remove (*((const char **) f->esp + 1));
      break;
    case SYS_OPEN:
      //printf("begin open\n");
      is_valid_ptr (f->esp, 1, false);
      f->eax = syscall_open (*((char **) f->esp + 1));
      break;
    case SYS_FILESIZE:
      is_valid_ptr (f->esp, 1, false);
      f->eax = syscall_filesize (*((int *) f->esp + 1));
      break;
    case SYS_READ:
      //printf("begin read\n");
      is_valid_ptr (f->esp, 3, false);
      check_memory (*((void **) f->esp + 2), *((unsigned *) f->esp + 3), true);
      f->eax = syscall_read (*((int *) f->esp + 1), (void *) (*((int *) f->esp + 2)), *((unsigned *) f->esp + 3));
      break;
    case SYS_WRITE:
      is_valid_ptr (f->esp, 3, false);
      check_memory (*((void **) f->esp + 2), *((unsigned *) f->esp + 3), false);
      f->eax = syscall_write (*((int *) f->esp + 1), (*((void **) f->esp + 2)), *((unsigned *) f->esp + 3));
      break;
    case SYS_SEEK:
      is_valid_ptr (f->esp, 2, false);
      syscall_seek (*((int *) f->esp + 1), *((unsigned *) f->esp + 2));
      break;
    case SYS_TELL:
      is_valid_ptr (f->esp, 1, false);
      f->eax = syscall_tell (*((int *) f->esp + 1));
      break;
    case SYS_CLOSE:
      is_valid_ptr (f->esp, 1, false);
      f->eax = syscall_close (*((int *) f->esp + 1));
      break;
    /* Prriject 3 */
    case SYS_MMAP:
      is_valid_ptr (f->esp, 2, false);
      //printf ("SYSCALL MMAP\n");

      f->eax = syscall_mmap (*((int *) f->esp + 1),
                             *((void **) f->esp + 2)); //FIXME:
      break;
    case SYS_MUNMAP:
      is_valid_ptr (f->esp, 1, false);
      //printf ("SYSCALL MUNMAP\n");

      syscall_munmap (*((int *) f->esp + 1)); //FIXME:
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

static void
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

static mapid_t
syscall_mmap (int fd, const void *addr)
{
  lock_acquire (&file_lock);
  if (fd == 0 || fd == 1) // fd cannot be 0 or 1, fail
    {
      lock_release (&file_lock);
      return -1;
    }
  struct file_descriptor *f
      = get_file_descriptor (fd); //  get file structer by fd
  if (file_length (f->file) == 0)
    {
      lock_release (&file_lock);
      return -1; // file length is 0, fail
    }
  if (addr == 0 || addr == NULL)
    {
      lock_release (&file_lock);
      return -1; // addr is 0 , fail
    }
  if (addr != pg_round_down (addr))
    {
      lock_release (&file_lock);
      return -1; // addr is not page aligned, fail
    }

  file_reopen (f->file); // reopen file

  // whether the addr is overlapping with exsiting file map
  for (int i = 0; i < file_length (f->file); i += PGSIZE)
    {
      if (page_lookup (&thread_current ()->sup_page_table, addr + i) != NULL)
        {
          lock_release (&file_lock);
          return -1;
        }
    }


  // 对于一个文件，创建 n 个补充页表，直到把这个文件装下为止, i是 offset
  for (int i = 0; i < file_length (f->file); i += PGSIZE)
    {

      struct sup_page_table_entry *spte
          = malloc (sizeof (struct sup_page_table_entry)); // 创建一个页表

      if (spte == NULL)
        {
          return -1;
        }                                            // 创建失败，返回 -1
      void *frame = frame_get (PAL_USER, spte);      // 获取一个空闲页面
      if (frame == NULL)
        {
          free (spte);
          lock_release (&file_lock);
          return -1;
        }

      // 插，进程补充页表，frame，file，offset = i， 大小为 pagesize 或者 size % pagesize， 可读可写
      if (!page_record (&thread_current ()->sup_page_table, addr, true, f->file,
                        i,
                        file_length (f->file) % PGSIZE == 0
                            ? PGSIZE
                            : file_length (f->file) % PGSIZE,
                        false))
        {
          //printf ("%s:%d, mmap failed\n", __func__, __LINE__);
          lock_release (&file_lock);
          return -1;
        }
    }
  struct mmap_descriptor *_mmap_descriptor
      = malloc (sizeof (struct mmap_descriptor));
  _mmap_descriptor->mapid = thread_current ()->next_mapid++;
  _mmap_descriptor->file = f->file;
  _mmap_descriptor->addr = pg_round_down (addr);
  _mmap_descriptor->file_size = file_length (f->file);
  list_push_back (&thread_current ()->mmap_list, &_mmap_descriptor->elem);
  lock_release (&file_lock);
  return _mmap_descriptor->mapid; //成功！返回 mapid
}

static void
syscall_munmap (mapid_t mapid)
{
  return;
  lock_acquire (&file_lock);
  struct mmap_descriptor *_mmap_descriptor = get_mmap_descriptor (mapid);
  if (_mmap_descriptor == NULL)
    {
      lock_release (&file_lock);
      return;
    }
  for (int i = _mmap_descriptor->addr;
       i < _mmap_descriptor->addr + _mmap_descriptor->file_size; i += PGSIZE)
    {
      struct sup_page_table_entry *spte
          = page_lookup (&thread_current ()->sup_page_table, i);
      if (spte == NULL)
        {
          lock_release (&file_lock);
          return;
        }
      //TODO: consider
      frame_free (spte->frame);
      page_free (&thread_current ()->sup_page_table, i);
    }
  //TODO: 是否需要占用这个文件 (reopen)
  list_remove (&_mmap_descriptor->elem);
  free (_mmap_descriptor);
}

static struct mmap_descriptor *
get_mmap_descriptor (mapid_t id)
{
  struct list_elem *e;
  struct mmap_descriptor *_mmap_descriptor;
  if (list_begin (&thread_current ()->mmap_list) == NULL)
    return NULL;

  for (e = list_begin (&thread_current ()->mmap_list);
       e != list_end (&thread_current ()->mmap_list); e = list_next (e))
    {
      _mmap_descriptor = list_entry (e, struct mmap_descriptor, elem);
      if (_mmap_descriptor == NULL || _mmap_descriptor->mapid < 1)
        return NULL;
      if (_mmap_descriptor->mapid == id)
        return _mmap_descriptor;
    }
  return NULL;
}