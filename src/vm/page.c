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
  const struct sup_page_table_entry *spte = hash_entry (e, struct sup_page_table_entry, hash_elem);
  return hash_bytes (&spte->user_vaddr, sizeof spte->user_vaddr);
}

/* return True if a is lower than b*/
static bool
spte_less (const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED)
{
  const struct sup_page_table_entry *spte_a = hash_entry (a, struct sup_page_table_entry, hash_elem);
  const struct sup_page_table_entry *spte_b = hash_entry (b, struct sup_page_table_entry, hash_elem);
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
    {
      return false; // already exist
    }

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

  if (hash_insert (spt, &spte->hash_elem) != NULL)
    {
      return false; // fail in hash_insert
    }
  return true;
}

bool
page_load (struct hash *spt, void *user_vaddr, bool write, void *esp)
{
  printf ("%s:%d \n", __FILE__, __LINE__);
  struct sup_page_table_entry *spte = page_lookup (spt, user_vaddr); // 在补充页表里找在不在
  if (spte == NULL)                                                  //|| (spte->present && spte->swapped) || (write && !spte->writable)
    {
      // 找不到 -> 创建一个新的空页表
      spte = malloc (sizeof (struct sup_page_table_entry));
      if (spte == NULL)
        return false; // fail in malloc

      void *frame = frame_get (PAL_USER, spte); // 去抓一段空的物理地址给这个页表
      if (frame == NULL)
        return false;      // fail in frame_get
      spte->frame = frame; // 把这个页填进去
      spte->user_vaddr = user_vaddr;
      if (!install_page (user_vaddr, frame, spte->writable))
        return false; // fail in install_page
      return true;
    }
};
                       
void
page_free (struct hash *spt, void *user_vaddr)
{
  struct sup_page_table_entry *spte = page_lookup (spt, user_vaddr);
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

  if (addr < PHYS_BASE && addr >= PHYS_BASE - STACK_MAX_SIZE && addr >= esp - 32)
    {
      printf ("%s:%d ,ADDR: %ud, PHYS_BASE: %ud \n", __FILE__, __LINE__, addr, PHYS_BASE);
      if (page_load (spt, addr, write, esp))
        return true; // 成功解决了
    }
  printf ("%s:%d ,ADDR: %u, PHYS_BASE: %u, ESP: %u , RD: %u\n", __FILE__, __LINE__, addr, PHYS_BASE, esp, pg_round_down (addr));
  return false; // 真的错了
}