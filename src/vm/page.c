#include "vm/page.h"
#include <hash.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include "threads/vaddr.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include "vm/frame.h"
#include "vm/swap.h"
#include "filesys/file.h"

//#define STACK_MAX_SIZE (0x800000) // 8 MB
#define STACK_LIMIT ((void *) PHYS_BASE - (0x800000))

/* return a hash value of page e */
static unsigned
spte_hash (const struct hash_elem *e, void *aux UNUSED)
{
  const struct sup_page_table_entry *spte
      = hash_entry (e, struct sup_page_table_entry, hash_elem);
  return hash_bytes (&spte->vaddr, sizeof spte->vaddr);
}

/* return True if a is lower than b*/
static bool
spte_less (const struct hash_elem *a, const struct hash_elem *b,
           void *aux UNUSED)
{
  const struct sup_page_table_entry *spte_a
      = hash_entry (a, struct sup_page_table_entry, hash_elem);
  const struct sup_page_table_entry *spte_b
      = hash_entry (b, struct sup_page_table_entry, hash_elem);
  return spte_a->vaddr < spte_b->vaddr;
}

static void
page_load_zero (struct sup_page_table_entry *spte, void *upage, void *kpage)
{
  memset (kpage, 0, PGSIZE);

  spte->vaddr = upage;
  spte->writable = true;
  spte->is_stack = true;
}

static void
page_destroy_entry (struct hash_elem *e, void *aux UNUSED)
{
  //printf ("%d: page_destroy_entry\n", thread_tid());
  struct sup_page_table_entry *spte
      = hash_entry (e, struct sup_page_table_entry, hash_elem);
  //printf("swap_id: %d\n", spte->swap_id);
  if (spte->swap_id != -1)
    {
      //printf("%d: to free swap: %d\n", thread_tid(), spte->swap_id);
      swap_free(spte->swap_id);
    }
  if (spte->frame)
    {
      //printf("%d: to free kpage: %p\n", thread_tid(), spte->frame->kpage);
      //palloc_free_page(spte->frame->kpage);
      //pagedir_clear_page(thread_current()->pagedir, spte->vaddr);
      if (spte->frame->owner == thread_current())
        frame_free(spte->frame);
    }
  free (spte);
  //printf("%d: destroyed spte\n", thread_tid());
}

static bool
page_load_swap (struct sup_page_table_entry *spte, void *kpage)
{
  //printf("thread: %p, begin swap in\n", thread_current());
  if(!swap_in(spte->swap_id, kpage))
    return false;
  //printf("thread: %d, swap in complete\n", thread_tid());
  return true;
}

static bool
page_load_file (struct sup_page_table_entry *spte, void *kpage)
{
  if (file_read_at (spte->file, kpage, spte->file_size, spte->file_ofs)
      != (off_t) spte->file_size)
    {
      //printf("failed load file\n");
      palloc_free_page(kpage);
      //frame_free (kpage);
      return false; // fail in file_read_at
    }

  memset (kpage + spte->file_size, 0, PGSIZE - spte->file_size);
  //printf("load to %p\n", kpage);
  return true;
}

void
page_init (struct hash *spt)
{
  hash_init (spt, spte_hash, spte_less, NULL);
}

void
page_destroy (struct hash *spt)
{
  //printf("%d: destroy hash\n", thread_tid());
  hash_destroy (spt, page_destroy_entry);
}

/* return the page which has the address */
struct sup_page_table_entry *
page_lookup (struct hash *spt, const void *addr)
{
  void *upage = pg_round_down (addr);
  struct sup_page_table_entry spte;
  struct hash_elem *e;

  spte.vaddr = upage;
  //printf ("lookup in spt: %p\n", spt);
  e = hash_find (spt, &spte.hash_elem);
  //printf ("lookup - ADDR: %p, UPAGE: %p, %s\n", addr, upage, e == NULL ? "NULL" : "NOT NULL");
  return e != NULL ? hash_entry (e, struct sup_page_table_entry, hash_elem)
                   : NULL;
}

bool
page_record (struct hash *spt, void *upage, bool writable,
             struct file *file, off_t ofs, uint32_t read_bytes, bool in_stack)
{
  ASSERT (pg_ofs (upage) == 0);
  if (page_lookup (spt, upage) != NULL)
    return false; // already exist

  struct sup_page_table_entry *spte
      = malloc (sizeof (struct sup_page_table_entry));
  if (spte == NULL)
    return false; // fail in malloc
  //printf("spte recorded: %p\n", upage);
  spte->frame = NULL;
  spte->vaddr = upage;
  spte->writable = writable;
  spte->is_stack = in_stack;
  spte->swap_id = -1;
  spte->file = file;
  spte->file_ofs = ofs;
  spte->file_size = read_bytes;

  if (hash_insert (spt, &spte->hash_elem) != NULL)
    {
      //printf ("page_record - hash_insert fail\n");
      free (spte);
      return false; // fail in hash_insert
    }

  return true;
}

bool
page_load (struct hash *spt, const void *vaddr, bool write, void *esp)
{
  void *upage = pg_round_down (vaddr);
  struct sup_page_table_entry *spte
      = page_lookup (spt, upage); // 在补充页表里找在不在
  struct frame_table_entry *frame = frame_get (PAL_USER, spte);
  if (frame == NULL)
    return false;
  //printf ("%d: page_load - frame: %p, upage: %p\n",thread_tid(), frame, upage);

  if (spte == NULL) //|| (spte->present && spte->swapped) || )
    {
      //printf ("page_load - page not found\n");
      if (vaddr < STACK_LIMIT || vaddr < esp - 32)
        return false; // fail in frame_get

      // 找不到 -> 创建一个新的空页表
      spte = malloc (sizeof (struct sup_page_table_entry));
      if (spte == NULL)
        return false; // fail in malloc    // fail in frame_get

      //printf("spte zeroed: %p\n", spte);

      page_load_zero (spte, upage, frame->kpage);

      //printf ("insert in spt: %p\n", spt);
      if (hash_insert (spt, &spte->hash_elem) != NULL)
        {
          //printf ("fail in hash_insert\n");
          free (spte);
          return false; // fail in hash_insert
        }
      //printf ("zero page inserted: %p\n", page_lookup (spt, upage));
    }
  else
    {
      if (spte->is_stack && (vaddr < STACK_LIMIT || vaddr < esp - 32))
        return false; // fail in frame_get

      if (write && !spte->writable)
        return false;
      //printf("swap or file: %p, INT: %d\n", spte->vaddr, spte->swap_id);
      if (spte->swap_id != -1)
        {
          ASSERT (spte->frame == NULL);
          if (!page_load_swap (spte, frame->kpage))
            return false;
        }
      else
        {
          // 打开文件
          ASSERT (spte->file != NULL);
          page_load_file (spte, frame->kpage);
        }
    }

  if (!install_page (upage, frame->kpage, spte->writable))
    {
      //printf("failed to install page %p", frame);
      palloc_free_page(frame->kpage);
      frame_free (frame);
      page_free (spt, spte);
      return false; // fail in install_page
    }

  spte->frame = frame; // 把这个页填进去
  spte->swap_id = -1;
  //printf("lazy loading success\n");
  return true;
};

void
page_free (struct hash *spt, const void *vaddr)
{
  struct sup_page_table_entry *spte = page_lookup (spt, vaddr);
  if (spte == NULL)
    {
      return; // fail in get_spte
    }
  hash_delete (&thread_current ()->sup_page_table, &spte->hash_elem);
  free (spte);
}

bool
page_fault_handler (struct hash *spt, const void *addr, bool write, void *esp)
{
  //printf("fault addr: %p\n", addr);
  if (addr == NULL || is_kernel_vaddr (addr)) // 有错就真的错
    return false;

  if (is_user_vaddr (addr)) // && addr >= STACK_LIMIT && addr >= esp - 32)
    {
      //printf("fake fault: %p\n", addr);
      if (page_load (spt, addr, write, esp))
        return true; // 成功解决了
    }
  return false; // 真的错了
}

void
page_print (struct hash *spt)
{
  struct hash_iterator i;
  hash_first (&i, spt);
  printf("========SUP_PAGE OF: %d========\n", thread_tid());
  while (hash_next (&i))
    {
      struct sup_page_table_entry *spte
          = hash_entry (hash_cur (&i), struct sup_page_table_entry, hash_elem);
      printf ("================================\n");
      printf ("vaddr : %p, is file: %d is stack: %d ", spte->vaddr,
              spte->file ? 1 : 0, spte->is_stack ? 1 : 0);
      if (spte->file)
        {
          printf ("size: %d, offset: %d ", spte->file_size, spte->file_ofs);
        }
      if (spte->frame)
        printf ("swap_id: %d, kpage: %p\n", spte->swap_id, spte->frame->kpage);
      else
        printf ("swap_id: %d, frame: %p\n", spte->swap_id, spte->frame);

      printf ("================================\n");
    }
}