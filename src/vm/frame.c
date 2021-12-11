#include "vm/frame.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "vm/page.h"
#include "vm/swap.h"
#include <stdio.h>

static struct hash frame_table;
struct hash_iterator clock;
static struct lock lock;

static unsigned
frame_hash (const struct hash_elem *e, void *aux UNUSED)
{
  const struct frame_table_entry *f
      = hash_entry (e, struct frame_table_entry, hash_elem);
  return hash_bytes (&f->kpage, sizeof f->kpage);
}

static bool
frame_less (const struct hash_elem *a_, const struct hash_elem *b_,
            void *aux UNUSED)
{
  const struct frame_table_entry *a
      = hash_entry (a_, struct frame_table_entry, hash_elem);
  const struct frame_table_entry *b
      = hash_entry (b_, struct frame_table_entry, hash_elem);
  return a->kpage < b->kpage;
}

static void *
frame_evict ()
{
  struct frame_table_entry *fte = NULL;
  struct hash_iterator clock;
  hash_first (&clock, &frame_table);
  while (hash_next (&clock))
    {
      fte = hash_entry (hash_cur (&clock), struct frame_table_entry, hash_elem);
      if (!lock_held_by_current_thread (&fte->lock)
          && !lock_try_acquire (&fte->lock))
        continue;

      /* Check if used */
      if (!fte->used)
        break;
      else
        fte->used = 0;
    }

  int index = swap_out (fte->kpage);
  if (index == -1)
    return NULL;

  fte->upage->swap_id = index; // needed for reclaiming
  fte->upage->frame = NULL;    // unmap upage
  void *ret = fte->kpage;
  if (lock_held_by_current_thread (&fte->lock))
    lock_release (&fte->lock);
  frame_clear (fte);
  return ret;
}

void
frame_init ()
{
  hash_init (&frame_table, frame_hash, frame_less, NULL);
  lock_init (&lock);
}

struct frame_table_entry *
frame_get (enum palloc_flags flags, struct sup_page_table_entry *upage)
{
  void *kpage = palloc_get_page (flags | PAL_ZERO);
  /* Not enough frames, evict one */
  if (kpage == NULL)
    kpage = frame_evict ();

  struct frame_table_entry *fte = malloc (sizeof (struct frame_table_entry));
  if (fte == NULL)
    {
      lock_release (&lock);
      palloc_free_page (kpage);
      return NULL;
    }
  lock_init (&fte->lock);

  /* Bookeeping for frames */
  fte->kpage = kpage;
  fte->upage = upage;
  fte->owner = thread_current ();
  fte->used = 1;
  hash_insert (&frame_table, &fte->hash_elem);
  return fte;
}

void
frame_clear (struct frame_table_entry *fte)
{
  hash_delete (&frame_table, &fte->hash_elem);
  pagedir_clear_page (fte->owner->pagedir, fte->upage->vaddr);
  free (fte);
}

void
frame_free (struct frame_table_entry *fte)
{
  hash_delete (&frame_table, &fte->hash_elem);
  palloc_free_page (fte->kpage);
  free (fte);
}

void
frame_destroy (struct frame_table_entry *fte)
{
  hash_delete (&frame_table, &fte->hash_elem);
  free (fte);
}