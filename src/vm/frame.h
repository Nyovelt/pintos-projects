#include <hash.h>
#include "threads/palloc.h"

struct frame_table_entry
{
    struct hash_elem hash_elem; // hash_elem for hash_list
    void *frame;                // frame pointer
    struct thread *owner;
    struct sup_page_entry *aux;
    // maybe other info
};

void frame_init (void); // Initialize frame table
void *frame_get (enum palloc_flags flags, struct sup_page_entry *upage);
struct frame_table_entry *frame_lookup (const void *frame);