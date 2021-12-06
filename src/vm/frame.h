#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <hash.h>
#include "threads/palloc.h"

struct frame_table_entry
{
    struct hash_elem hash_elem; // hash_elem for hash_list
    void *frame;                // frame pointer
    struct thread *owner;       // owner of the frame
    struct sup_page_entry *aux; // auxiliary page entry
    // maybe other info
    bool dirty_bit;
};

void frame_init (void); // Initialize frame table
void *frame_get (enum palloc_flags flags, struct sup_page_entry *upage);
void frame_evict (enum palloc_flags flags, struct sup_page_entry *upage);
struct frame_table_entry *frame_lookup (const void *frame);

void frame_free (struct frame_table_entry *fte);

#endif /* vm/frame.h */