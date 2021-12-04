#include <hash.h>
#include "threads/palloc.h"

struct frame_table_entry
{
    struct hash_elem hash_elem; // hash_elem for hash_list
    void * frame;                // frame pointer
    struct thread *owner;
    struct sup_page_entry *aux;
    // maybe other info
};

void frame_table_init (void); // Initialize frame table
void *frame_get_frame (enum palloc_flags flags, struct sup_page_entry *upage);