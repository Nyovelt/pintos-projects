#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <stdbool.h>
#include <stdint.h>
#include <hash.h>
#include "vm/frame.h"
#include "filesys/off_t.h"

struct sup_page_table_entry
{
    struct hash_elem hash_elem;
    struct frame_table_entry *frame;
    const void *vaddr;          // virtual address of the page
    bool writable;        // is the page writable?
    bool swapped;         // is the page swapped?
    uint64_t access_time; // time of last access

    /* For fole operations */
    struct file *file;
    off_t file_ofs;
    uint32_t file_size;
};

#endif /* vm/page.h */

void page_init (struct hash *spt);
bool page_record (struct hash *spt, const void *upage, bool writable, struct file *file, off_t ofs, uint32_t read_bytes); // TODO: more args?
bool page_load (struct hash *spt, const void *vaddr, bool write);
void page_free (struct hash *spt, const void *vaddr);
bool page_fault_handler (struct hash *spt, const void *addr, bool write, void *esp);