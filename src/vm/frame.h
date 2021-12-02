#include <list.h>


struct list frame_table; // We use list to store frame table

struct frames_table_entry
{
    struct list_elem elem; // for list
    void *frame;           // frame address
};
