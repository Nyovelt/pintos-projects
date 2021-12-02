#include <hash.h>

struct hash frame_table; // We use hash_list to store frame table

struct  frame_table_elem{
    struct hash_elem hash_elem; // hash_elem for hash_list
    void * frame; // frame pointer


};


void frame_table_init(void); // Initialize frame table
