#include <stdbool.h>
#include <stdint.h>

struct sup_page_table_entry
{
  void *user_vaddr;
  void *phys_addr;
  bool present;
  bool writable;
  bool dirty;
  bool accessed;
  uint64_t access_time;
};