#include "vm/frame.h"
#include "threads/thread.h"

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
frame_table_init ()
{
  hash_init (&frame_table, frame_hash, frame_less, NULL);
  lock_init (&lock);
}

struct frame_table_entry *
frame_table_lookup (const void *frame)
{
  struct frame_table_entry f;
  struct hash_elem *e;

  f.frame = frame;
  e = hash_find (&frame_table, &f.hash_elem);
  return e != NULL ? hash_entry (e, struct frame_table_entry, hash_elem) : NULL;
}