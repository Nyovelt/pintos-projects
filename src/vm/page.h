#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <stdbool.h>
#include <stdint.h>
#include <hash.h>

struct sup_page_table_entry
{
    struct hash_elem hash_elem;
    void *user_vaddr;     // virtual address of the page
    void *phys_addr;      // physical address of the page
    bool present;         // is the page present in memory?
    bool writable;        // is the page writable?
    bool dirty;           // has the page been written to?
    bool accessed;        // has the page been accessed?
    uint64_t access_time; // time of last access
};

#endif /* vm/page.h */

void page_init (void);
void *page_create (void *user_vaddr); // TODO: more args?
void page_free (void *user_vaddr);
struct sup_page_table_entry *page_lookup (const void *user_vaddr);
bool page_fault_handler (const void *addr);