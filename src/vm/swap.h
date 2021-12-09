#ifndef VM_SWAP_H
#define VM_SWAP_H

#include <stdint.h>

void swap_init (void);
uint32_t swap_out(void *frame);
bool swap_in(uint32_t index, void *frame);

#endif /* vm/swap.h */