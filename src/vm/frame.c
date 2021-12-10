#include "vm/frame.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "vm/page.h"
#include "vm/swap.h"
#include <stdio.h>

unsigned short lfsr = 0xACE1u;
unsigned bit;
static unsigned
rand ()
{
  bit = ((lfsr >> 0) ^ (lfsr >> 2) ^ (lfsr >> 3) ^ (lfsr >> 5)) & 1;
  return lfsr = (lfsr >> 1) | (bit << 15);
}

static struct hash frame_table;
struct hash_iterator clock;
static struct lock lock;
//int try[10] = {0, 20, 44, 55, 66, 65, 68, 325, 65, 365};

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
  //lock_acquire (&lock); // Not sure
  //printf("begin evict\n");
  /*struct frame_table_entry *fte
      = hash_entry (hash_cur (&clock), struct frame_table_entry, hash_elem);
  if (fte == NULL)
    {
      //printf("get to first before loop\n");
      hash_first (&clock, &frame_table);
      fte = hash_entry (hash_next (&clock), struct frame_table_entry,
                        hash_elem);
    }
  while (false)
    {
      //printf("set false: FTE:%p, KPAGE: %p\n", fte, fte->kpage);
      fte->used = false;
      //printf("find next\n");
      fte = hash_entry (hash_next (&clock), struct frame_table_entry,
                        hash_elem);
      if (fte == NULL)
        {
          //printf("back to first\n");
          hash_first (&clock, &frame_table);
          fte = hash_entry (hash_next (&clock), struct frame_table_entry,
                            hash_elem);
        }
      //printf("loop next: FTE:%p, KPAGE: %p\n", fte, fte->kpage);
    }
  hash_next (&clock);*/

  //printf ("Evicting: FTE:%p, KPAGE: %p\n", fte, fte->kpage);
  lock_acquire (&lock);
  int evict_num = rand () % (hash_size (&frame_table) - 1);
  //printf ("evict_num: %d, total: %d\n", evict_num,
          //hash_size (&frame_table) - 1);
  hash_first (&clock, &frame_table);
  hash_next (&clock);
  for (int i = 0; i < evict_num; i++)
    hash_next (&clock);

  struct frame_table_entry *fte
      = hash_entry (hash_cur (&clock), struct frame_table_entry, hash_elem);

  int index = swap_out (fte->kpage);
  if (index == -1)
    {
      lock_release (&lock);
      return NULL;
    }
  //printf ("Evicted frame %p with upage %p INDEX: %d\n", fte->kpage,
          //fte->upage->vaddr, index);
  fte->upage->swap_id = index;
  fte->upage->frame = NULL;
  void *ret = fte->kpage;
  lock_release (&lock);
  frame_free (fte);
  //printf ("evicted page %p\n", fte->kpage);
  //lock_release (&lock);
  //printf ("evicted page %p\n", fte->kpage);
  return ret;
}

void
frame_init ()
{
  hash_init (&frame_table, frame_hash, frame_less, NULL);
  lock_init (&lock);
  /*hash_first (&clock, &frame_table);
  hash_next (&clock);*/
}

struct frame_table_entry *
frame_get (enum palloc_flags flags, struct sup_page_table_entry *upage)
{
  /* Validating the address */
  /* Within 32 bytes of the stack pointer */
  // if (!is_user_vaddr (upage) || !is_user_vaddr ((void *) upage + sizeof (void *) - 1))
  //   return NULL;
  lock_acquire (&lock);
  void *kpage = palloc_get_page (flags | PAL_ZERO);
  if (kpage == NULL)
    {
      lock_release (&lock);
      kpage = frame_evict();
      //printf("alloc again\n");
      ASSERT (kpage != NULL)
      lock_acquire (&lock);
      //printf("new kpage: %p \n", frame);
    }
  //printf ("Allocated frame %p with upage %p\n", frame, upage->vaddr);

  struct frame_table_entry *fte = malloc (sizeof (struct frame_table_entry));
  if (fte == NULL)
    {
      lock_release (&lock);
      palloc_free_page (kpage);
      return NULL;
    }

  /* Bookeeping for frames */
  fte->kpage = kpage;
  fte->upage = upage;
  fte->owner = thread_current ();
  fte->used = 1;
  hash_insert (&frame_table, &fte->hash_elem);
  lock_init (&fte->lock);
  lock_release (&lock);
  //printf ("Allocated frame %p for vaddr %p\n", frame, upage->vaddr);
  return fte;
}

void
frame_free (struct frame_table_entry *fte)
{
  lock_acquire (&lock);
  hash_delete (&frame_table, &fte->hash_elem);
  pagedir_clear_page(fte->owner->pagedir, fte->upage->vaddr);
  free (fte);
  lock_release (&lock);
}