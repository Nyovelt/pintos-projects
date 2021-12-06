#include <hash.h>
#include "stdbool.h"
#include "vm/page.h"
#include "threads/vaddr.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "userprog/process.h"


static struct hash sup_page_table;
static struct lock lock;

#define STACK_MAX_SIZE (0x800000)

/* return a hash value of page e */
static unsigned
spte_hash (const struct hash_elem *e, void *aux UNUSED)
{
  struct sup_page_table_entry *spte = hash_entry (e, struct sup_page_table_entry, hash_elem);
  return hash_bytes (&spte->user_vaddr, sizeof spte->user_vaddr);
}

/* return True if a is lower than b*/
static bool
spte_less (const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED)
{
  struct sup_page_table_entry *spte_a = hash_entry (a, struct sup_page_table_entry, hash_elem);
  struct sup_page_table_entry *spte_b = hash_entry (b, struct sup_page_table_entry, hash_elem);
  return spte_a->user_vaddr < spte_b->user_vaddr;
}

static struct sup_page_table_entry *
page_entry_init (void *user_vaddr, bool isDirty, bool isAccessed)
{
  // TODO: lock? directly use malloc?
  struct sup_page_table_entry *spte = malloc (sizeof (struct sup_page_table_entry));
  if (spte == NULL)
      return NULL; // fail in malloc
  spte->user_vaddr = user_vaddr;
  spte->dirty = isDirty;
  spte->accessed = isAccessed;
  return spte;
}

void
page_init ()
{
  hash_init (&sup_page_table, spte_hash, spte_less, NULL);
  lock_init (&lock);
}

void *
page_create (void *user_vaddr)
{
  lock_acquire (&lock);
  struct sup_page_table_entry *spte = page_entry_init (user_vaddr, false, false);
  if (spte == NULL)
    {
      lock_release (&lock);
      return NULL; // fail in init_page
    }
  if (hash_insert (&thread_current ()->sup_page_table, &spte->hash_elem) == NULL)
    {
      lock_release (&lock);
      return NULL; // fail in hash_insert
    }
  // get a frame
  struct frame_table_entry *fe = frame_get (PAL_USER, spte);
  if (fe == NULL)
    {
      lock_release (&lock);
      return NULL; // fail in frame_get
    }

  // install
  spte->frame = fe;
  // TODO: and other things...
  if (!install_page (user_vaddr, fe->frame, true))
    {
      page_free (user_vaddr);
      lock_release (&lock);
      return NULL; // fail in install_page
    }
  lock_release (&lock);
  return spte;
};

void
page_free (void *user_vaddr)
{
  lock_acquire (&lock);
  struct sup_page_table_entry *spte = page_lookup (user_vaddr);
  if (spte == NULL)
    {
      lock_release (&lock);
      return; // fail in get_spte
    }
  hash_delete (&thread_current ()->sup_page_table, &spte->hash_elem);
  free (spte);
  lock_release (&lock);
}

/* return the page which has the address */
struct sup_page_table_entry *
page_lookup (const void *user_vaddr)
{
  struct sup_page_table_entry spte;
  struct hash_elem *e;

  spte.user_vaddr = user_vaddr;
  e = hash_find (&thread_current ()->sup_page_table, &spte.hash_elem);
  return e != NULL
             ? hash_entry (e, struct sup_page_table_entry, hash_elem)
             : NULL;
}

bool
page_fault_handler (const void *addr, void *esp)

{
  //return true;
  if (addr == NULL || is_kernel_vaddr (addr)) //TODO: discuss
    return false;                             // fail in addr

  // check addr is in spt
  // struct sup_page_table_entry *spte = page_lookup (addr);

  // if it is in spte, then load data
  // TODO: wait for swap

  // if it is not in spte
  // if it is stack grow


  if (addr < PHYS_BASE && addr >= PHYS_BASE - STACK_MAX_SIZE && addr >= esp - 32) //TODO: is it uint32_t?
    {
      // stack grow
      struct sup_page_table_entry *spte = page_create (addr);
      if (spte != NULL)
        return true;
    }
  else
    {
      return false; // real page fault
    }
}