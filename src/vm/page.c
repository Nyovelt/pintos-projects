#include "vm/page.h"
#include <hash.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include "vm/frame.h"
#include "vm/swap.h"
#include "filesys/file.h"

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
  struct sup_page_table_entry *spte
      = hash_entry (e, struct sup_page_table_entry, hash_elem);
  if (spte->swap_id != -1)
    swap_free (spte->swap_id);
  if (spte->frame && spte->frame->owner == thread_current ())
    frame_destroy (spte->frame);
  free (spte);
}

static bool
page_load_swap (struct sup_page_table_entry *spte, void *kpage)
{
  if (!swap_in (spte->swap_id, kpage))
    return false;
  return true;
}

static bool
page_load_file (struct sup_page_table_entry *spte, void *kpage)
{
  if (file_read_at (spte->file, kpage, spte->file_size, spte->file_ofs)
      != (off_t) spte->file_size)
    {
      palloc_free_page (kpage);
      return false; // fail in file_read_at
    }

  memset (kpage + spte->file_size, 0, PGSIZE - spte->file_size);
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
  e = hash_find (spt, &spte.hash_elem);
  return e != NULL ? hash_entry (e, struct sup_page_table_entry, hash_elem)
                   : NULL;
}

bool
page_record (struct hash *spt, void *upage, bool writable, struct file *file,
             off_t ofs, uint32_t read_bytes, bool in_stack)
{
  ASSERT (pg_ofs (upage) == 0);
  if (page_lookup (spt, upage) != NULL)
    return false; // already exist

  struct sup_page_table_entry *spte
      = malloc (sizeof (struct sup_page_table_entry));
  if (spte == NULL)
    return false; // fail in malloc
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

  if (spte == NULL)
    {
      /* Validating address */
      if (vaddr < STACK_LIMIT || vaddr < esp - 32)
        return false;

      /* Need to create a new empty page */
      spte = malloc (sizeof (struct sup_page_table_entry));
      if (spte == NULL)
        return false; // fail in malloc    // fail in frame_get

      page_load_zero (spte, upage, frame->kpage);

      if (hash_insert (spt, &spte->hash_elem) != NULL)
        {
          free (spte);
          return false; // fail in hash_insert
        }
    }
  else
    {
      if (spte->is_stack && (vaddr < STACK_LIMIT || vaddr < esp - 32))
        return false; // fail in frame_get

      if (write && !spte->writable)
        return false;
      lock_acquire (&frame->lock);
      if (spte->swap_id != -1)
        {
          if (!page_load_swap (spte, frame->kpage))
            return false;
        }
      else
        {
          if (!page_load_file (spte, frame->kpage))
            return false;
        }
      lock_release (&frame->lock);
    }

  if (!install_page (upage, frame->kpage, spte->writable))
    {
      palloc_free_page (frame->kpage);
      frame_free (frame);
      page_free (spt, spte);
      return false; // fail in install_page
    }

  /* Bookkeeping */
  spte->frame = frame;
  spte->swap_id = -1;
  return true;
};

void
page_free (struct hash *spt, const void *vaddr)
{
  struct sup_page_table_entry *spte = page_lookup (spt, vaddr);
  if (spte == NULL)
    return; // fail in get_spte
  hash_delete (&thread_current ()->sup_page_table, &spte->hash_elem);
  free (spte);
}

bool
page_fault_handler (struct hash *spt, const void *addr, bool write, void *esp)
{
  if (addr == NULL || is_kernel_vaddr (addr)) // Real fault
    return false;

  if (is_user_vaddr (addr))
    {
      if (page_load (spt, addr, write, esp))
        return true; // can continue execution
    }
  return false; // Real fault
}

void
page_print (struct hash *spt)
{
  struct hash_iterator i;
  hash_first (&i, spt);
  printf ("========SUP_PAGE OF: %d========\n", thread_tid ());
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