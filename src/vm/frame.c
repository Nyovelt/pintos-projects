#include "vm/frame.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"

static struct hash frame_table;
static struct lock lock;

static unsigned
frame_hash (const struct hash_elem *e, void *aux UNUSED)
{
  const struct frame_table_entry *f = hash_entry (e, struct frame_table_entry, hash_elem);
  return hash_bytes (&f->frame, sizeof f->frame);
}

static bool
frame_less (const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED)
{
  const struct frame_table_entry *a = hash_entry (a_, struct frame_table_entry, hash_elem);
  const struct frame_table_entry *b = hash_entry (b_, struct frame_table_entry, hash_elem);
  return a->frame < b->frame;
}

void
frame_init ()
{
  hash_init (&frame_table, frame_hash, frame_less, NULL);
  lock_init (&lock);
}

void *
frame_get (enum palloc_flags flags, struct sup_page_entry *upage)
{
  /* Validating the address */
  /* Within 32 bytes of the stack pointer */
  if (!is_user_vaddr (upage) || !is_user_vaddr ((void *) upage + sizeof (void *) - 1))
    return NULL;

  lock_acquire (&lock);
  void *frame = palloc_get_page (flags);
  if (frame == NULL)
    {
      lock_release (&lock);
      return NULL;
    }

  struct frame_table_entry *fte = malloc (sizeof (struct frame_table_entry));
  if (fte == NULL)
    {
      palloc_free_page (frame);
      lock_release (&lock);
      return NULL;
    }

  /* Bookeeping for frames */
  fte->frame = frame;
  fte->aux = upage;
  fte->owner = thread_current ();
  hash_insert (&frame_table, &fte->hash_elem);

  lock_release (&lock);
  return frame;
}

struct frame_table_entry *
frame_lookup (const void *frame)
{
  struct frame_table_entry f;
  struct hash_elem *e;

  f.frame = frame;
  e = hash_find (&frame_table, &f.hash_elem);
  return e != NULL ? hash_entry (e, struct frame_table_entry, hash_elem) : NULL;
}