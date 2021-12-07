#include <hash.h>
#include "stdbool.h"
#include "vm/page.h"
#include "vm/frame.h"
#include "threads/vaddr.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "userprog/process.h"

#define STACK_MAX_SIZE (0x800000) // 8 MB

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

void
page_init (struct hash *spt)
{
  hash_init (spt, spte_hash, spte_less, NULL);
}

/* return the page which has the address */
static struct sup_page_table_entry *
page_lookup (struct hash *spt ,const void *vaddr)
{
  struct sup_page_table_entry spte;
  struct hash_elem *e;

  spte.user_vaddr = vaddr;
  e = hash_find (spt, &spte.hash_elem);
  return e != NULL
             ? hash_entry (e, struct sup_page_table_entry, hash_elem)
             : NULL;
}

/*static struct sup_page_table_entry *
page_entry_init (struct hash *spt, void *user_vaddr, bool isDirty, bool isAccessed)
{
  // TODO: lock? directly use malloc?
  struct sup_page_table_entry *spte = malloc (sizeof (struct sup_page_table_entry));
  if (spte == NULL)
    return NULL; // fail in malloc
  spte->user_vaddr = user_vaddr;
  return spte;
}*/

bool
page_record (struct hash *spt, void *upage, bool writable, struct file *file, off_t ofs, uint32_t read_bytes)
{
  if (page_lookup (spt, upage) != NULL)
    return false; // already exist

  struct sup_page_table_entry *spte = malloc (sizeof (struct sup_page_table_entry));
  if (spte == NULL)
    return false; // fail in malloc

  spte->user_vaddr = upage;
  spte->phys_addr = NULL;
  spte->present = false;
  spte->writable = writable;
  spte->swapped = false;
  spte->file = file;
  spte->file_ofs = ofs;
  spte->file_end = read_bytes;

  if (hash_insert (spt, &spte->hash_elem) == NULL)
    return false; // fail in hash_insert

  return true;
}

bool
page_load (struct hash *spt, void *user_vaddr, bool write)
{
  struct sup_page_table_entry *spte = page_lookup (spt, user_vaddr);
  if (spte == NULL || (spte->present && spte->swapped) || (write && !spte->writable))
    return false;

  void *frame = frame_get()
};

void
page_free (struct hash *spt, void *user_vaddr)
{
  struct sup_page_table_entry *spte = page_lookup (user_vaddr);
  if (spte == NULL)
    {
      return; // fail in get_spte
    }
  hash_delete (&thread_current ()->sup_page_table, &spte->hash_elem);
  free (spte);
}

bool
page_fault_handler (struct hash *spt, const void *addr, void *esp)

{
  //return true;
  if (addr == NULL || is_kernel_vaddr (addr)) // TODO: discuss
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

  return false; // real page fault
}