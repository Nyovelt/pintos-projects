#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <stdbool.h>
#include <stdint.h>

struct sup_page_table_entry
{
  void *user_vaddr; // virtual address of the page
  void *phys_addr; // physical address of the page
  bool present; // is the page present in memory?
  bool writable; // is the page writable?
  bool dirty; // has the page been written to?
  bool accessed; // has the page been accessed?
  uint64_t access_time; // time of last access
};

#endif /* vm/page.h */