#ifndef VM_SWAP_H
#define VM_SWAP_H

#include <stdbool.h>
#include <stdint.h>

void swap_init (void);
int swap_out (void *frame);
bool swap_in (uint32_t index, void *frame);

#endif /* vm/swap.h */