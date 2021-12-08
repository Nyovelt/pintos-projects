#include <hash.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include "vm/page.h"
#include "vm/frame.h"
#include "threads/vaddr.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "userprog/process.h"
#include "filesys/file.h"

//#define STACK_MAX_SIZE (0x800000) // 8 MB
#define STACK_LIMIT ((void *) PHYS_BASE - (0x800000))

/* return a hash value of page e */
static unsigned
spte_hash (const struct hash_elem *e, void *aux UNUSED)
{
  const struct sup_page_table_entry *spte = hash_entry (e, struct sup_page_table_entry, hash_elem);
  return hash_bytes (&spte->vaddr, sizeof spte->vaddr);
}

/* return True if a is lower than b*/
static bool
spte_less (const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED)
{
  const struct sup_page_table_entry *spte_a = hash_entry (a, struct sup_page_table_entry, hash_elem);
  const struct sup_page_table_entry *spte_b = hash_entry (b, struct sup_page_table_entry, hash_elem);
  return spte_a->vaddr < spte_b->vaddr;
}

void
page_init (struct hash *spt)
{
  hash_init (spt, spte_hash, spte_less, NULL);
}

/* return the page which has the address */
static struct sup_page_table_entry *
page_lookup (struct hash *spt, const void *addr)
{
  struct sup_page_table_entry spte;
  struct hash_elem *e;

  spte.vaddr = addr;
  e = hash_find (spt, &spte.hash_elem);
  return e != NULL
             ? hash_entry (e, struct sup_page_table_entry, hash_elem)
             : NULL;
}

bool
page_record (struct hash *spt, const void *upage, bool writable, struct file *file, off_t ofs, uint32_t read_bytes, bool in_stack)
{
  if (page_lookup (spt, upage) != NULL)
    {
      return false; // already exist
    }

  struct sup_page_table_entry *spte = malloc (sizeof (struct sup_page_table_entry));
  if (spte == NULL)
    return false; // fail in malloc
  spte->vaddr = upage;
  spte->writable = writable;
  spte->swapped = false;
  spte->is_stack = in_stack;
  spte->file = file;
  spte->file_ofs = ofs;
  spte->file_size = read_bytes;

  if (hash_insert (spt, &spte->hash_elem) != NULL)
    {
      free (spte);
      return false; // fail in hash_insert
    }

  //printf ("recorded. %s:%d ,ADDR: %p\n", __FILE__, __LINE__, upage);
  return true;
}

bool
page_load (struct hash *spt, const void *vaddr, bool write, void *esp)
{
  void *upage = pg_round_down (vaddr);
  struct sup_page_table_entry *spte = page_lookup (spt, upage); // 在补充页表里找在不在
  void *frame = NULL;
  //printf ("to load. %s:%d, UPAGE: %p\n", __FILE__, __LINE__, upage);

  if (spte == NULL) //|| (spte->present && spte->swapped) || )
    {
      if (vaddr < STACK_LIMIT || vaddr < esp - 32)
          return false; // fail in frame_get

      // 找不到 -> 创建一个新的空页表
      spte = malloc (sizeof (struct sup_page_table_entry));
      if (spte == NULL)
        return false; // fail in malloc    // fail in frame_get

      frame = frame_get (PAL_USER, spte); // 去抓一段空的物理地址给这个页表
      memset (frame, 0, PGSIZE);
      //printf ("zeroed page, %s:%d\n, UPAGE: %p\n, ESP: %p", __FILE__, __LINE__, upage, esp);
      spte->writable = true;
      spte->is_stack = true;
    }
  else
    {
      if (spte->is_stack && (vaddr < STACK_LIMIT || vaddr < esp - 32))
          return false; // fail in frame_get

      if (write && !spte->writable)
        return false;

      if (spte->swapped)
        {
          // then it is swap
        }
      else
        {
          // 打开文件
          if (spte->file == NULL)
            return false; // fail in check file
          //printf ("read file, %s:%d\n", __FILE__, __LINE__);
          frame = frame_get (PAL_USER, spte); // 去抓一段空的物理地址给这个页表

          if (file_read_at (spte->file, frame, spte->file_size, spte->file_ofs) != (off_t) spte->file_size)
            {
              frame_free (frame);
              return false; // fail in file_read_at
            }
          memset (frame + spte->file_size, 0, PGSIZE - spte->file_size);
        }
      //printf ("read file, %s:%d\n", __FILE__, __LINE__);
    }

  if (frame == NULL)
    return false;

  if (!install_page (upage, frame, spte->writable))
    {
      frame_free (frame);
      return false; // fail in install_page
    }

  spte->frame = frame; // 把这个页填进去
  spte->vaddr = upage;
  spte->swapped = false;
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
      //printf ("fake fault. %s:%d ,ADDR: %p, UPPER: %p, LOWER: %p , STACK: %p\n", __FILE__, __LINE__, addr, PHYS_BASE, STACK_LIMIT, esp - 32);
      if (page_load (spt, addr, write, esp))
        return true; // 成功解决了
    }
  //printf ("real fault. %s:%d ,ADDR: %p, UPPER: %p, LOWER: %p , STACK: %p\n", __FILE__, __LINE__, addr, PHYS_BASE, STACK_LIMIT, esp - 32);
  return false; // 真的错了
}