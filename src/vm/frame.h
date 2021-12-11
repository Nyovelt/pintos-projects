#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <hash.h>
#include "threads/palloc.h"
#include "threads/synch.h"

struct frame_table_entry
{
    struct hash_elem hash_elem; // hash_elem for hash_list
    struct lock lock;
    void *kpage;                        // frame pointer
    struct thread *owner;               // owner of the frame
    struct sup_page_table_entry *upage; // auxiliary page entry

    /* For clock algorithm */
    bool used;
    bool dirty;
};

void frame_init (void); // Initialize frame table
struct frame_table_entry *frame_get (enum palloc_flags flags,
                                     struct sup_page_table_entry *upage);

void frame_clear (struct frame_table_entry *fte);   // Marks UPAGE "not present"
void frame_free (struct frame_table_entry *fte);    // use palloc_free_page
void frame_destroy (struct frame_table_entry *fte); // just destroy entry

#endif /* vm/frame.h */