#include "vm/page.h"
#include <hash.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include "threads/vaddr.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "userprog/process.h"
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
page_load_zero (struct sup_page_table_entry *spte, void *upage, void *frame)
{
  memset (frame, 0, PGSIZE);

  spte->vaddr = upage;
  spte->writable = true;
  spte->is_stack = true;
}

static bool
page_load_swap (struct sup_page_table_entry *spte, void *frame)
{
  if(!swap_in(spte->swap_id, frame))
    return false;
  spte->frame = frame;
  //printf("swap in %d\n", spte->swap_id);
  return true;
}

static bool
page_load_file (struct sup_page_table_entry *spte, void *frame)
{
  if (file_read_at (spte->file, frame, spte->file_size, spte->file_ofs)
      != (off_t) spte->file_size)
    {
      frame_free (frame);
      return false; // fail in file_read_at
    }

  memset (frame + spte->file_size, 0, PGSIZE - spte->file_size);
  return true;
}

void
page_init (struct hash *spt)
{
  hash_init (spt, spte_hash, spte_less, NULL);
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
page_record (struct hash *spt, const void *upage, bool writable,
             struct file *file, off_t ofs, uint32_t read_bytes, bool in_stack)
{
  ASSERT (pg_ofs (upage) == 0);
  if (page_lookup (spt, upage) != NULL)
    return false; // already exist

  struct sup_page_table_entry *spte
      = malloc (sizeof (struct sup_page_table_entry));
  if (spte == NULL)
    return false; // fail in malloc
  //printf("spte recorded: %p\n", spte);
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

int
checksum (void *frame)
{
}

bool
page_load (struct hash *spt, const void *vaddr, bool write, void *esp)
{
  void *upage = pg_round_down (vaddr);
  struct sup_page_table_entry *spte
      = page_lookup (spt, upage); // 在补充页表里找在不在
  void *frame = frame_get (PAL_USER, spte);
  //printf ("page_load - frame: %p, upage: %p\n", frame, upage);

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

      page_load_zero (spte, upage, frame);

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
      //printf("%p, BOOL: %d, INT: %d\n", spte, spte->swapped, spte->swap_id);
      if (spte->swap_id != -1)
        {
          ASSERT (spte->frame == NULL);
          if (!page_load_swap (spte, frame))
            return false;
        }
      else
        {
          // 打开文件
          ASSERT (spte->file != NULL);
          page_load_file (spte, frame);
        }
    }

  if (frame == NULL || !install_page (upage, frame, spte->writable))
    {
      frame_free (frame);
      page_free (spt, spte);
      return false; // fail in install_page
    }

  spte->frame = frame; // 把这个页填进去
  spte->swap_id = -1;
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
  if (addr == NULL || is_kernel_vaddr (addr)) // 有错就真的错
    return false;

  if (is_user_vaddr (addr)) // && addr >= STACK_LIMIT && addr >= esp - 32)
    {
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

      printf ("frame: %p\n", spte->frame);

      printf ("================================\n");
    }
}