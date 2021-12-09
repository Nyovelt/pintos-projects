#include "vm/frame.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "vm/page.h"
#include "vm/swap.h"
#include <stdio.h>

static struct hash frame_table;
static struct lock global_lock;

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

static bool
frame_evict ()
{
  //lock_acquire (&lock); // Not sure
  struct hash_iterator i;
  hash_first (&i, &frame_table);
  hash_next (&i);
  struct frame_table_entry *fte
      = hash_entry (hash_cur (&i), struct frame_table_entry, hash_elem);
  int index = swap_out (fte->kpage);
  if (index == -1)
    {
      return false;
    }
  //printf ("Evicted frame %p with upage %p\n", fte->kpage, fte->upage->vaddr);
  fte->upage->swap_id = index;
  fte->upage->frame = NULL;
  frame_free (fte);
  //printf ("evicted page %p\n", fte->kpage);
  //lock_release (&lock);
  //printf ("evicted page %p\n", fte->kpage);
  return true;
}

void
frame_init ()
{
  hash_init (&frame_table, frame_hash, frame_less, NULL);
}

void *
frame_get (enum palloc_flags flags, struct sup_page_table_entry *upage)
{
  /* Validating the address */
  /* Within 32 bytes of the stack pointer */
  // if (!is_user_vaddr (upage) || !is_user_vaddr ((void *) upage + sizeof (void *) - 1))
  //   return NULL;
  void *frame = palloc_get_page (flags);
  if (frame == NULL)
    {
      if (!frame_evict ())
        return NULL;
      //printf("alloc again\n");
      frame = palloc_get_page (flags);
      //printf("new kpage: %p \n", frame);
      ASSERT (frame != NULL)
    }
  //printf ("Allocated frame %p with upage %p\n", frame, upage->vaddr);

  struct frame_table_entry *fte = malloc (sizeof (struct frame_table_entry));
  if (fte == NULL)
    {
      palloc_free_page (frame);
      return NULL;
    }

  /* Bookeeping for frames */
  fte->kpage = frame;
  fte->upage = upage;
  fte->owner = thread_current ();
  hash_insert (&frame_table, &fte->hash_elem);
  lock_init (&fte->lock);

  //printf ("Allocated frame %p for vaddr %p\n", frame, upage->vaddr);
  return frame;
}

void
frame_free (struct frame_table_entry *fte)
{
  hash_delete (&frame_table, &fte->hash_elem);
  palloc_free_page (fte->kpage);
  free (fte);
}