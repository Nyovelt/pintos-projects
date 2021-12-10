#include "vm/swap.h"
#include <debug.h>
#include <bitmap.h>
#include "devices/block.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "vm/page.h"
#include "vm/frame.h"
#include <stdio.h>

#define BLOCKS_PER_PAGE (PGSIZE / BLOCK_SECTOR_SIZE)
#define USED true
#define FREE false

static struct block *swap_block;
static struct bitmap *sectors;
static struct lock lock;

void
swap_init ()
{
  swap_block = block_get_role (BLOCK_SWAP);
  sectors = bitmap_create (block_size (swap_block));
  lock_init (&lock);
}

int
swap_out (void *frame)
{
  //printf("to swap_out: %p, %d\n", frame);
  if (frame == NULL || !is_kernel_vaddr (frame))
    return -1;

  //printf ("swap_out\n");
  lock_acquire (&lock);
  int index = bitmap_scan_and_flip (sectors, 0, BLOCKS_PER_PAGE, FREE);
  lock_release (&lock);
  //printf ("swap_out: index = %d\n", index);
  if (index == (int) BITMAP_ERROR)
    return -1;

  for (int i = 0; i < BLOCKS_PER_PAGE; i++)
    block_write (swap_block, index + i, frame + i * BLOCK_SECTOR_SIZE);
  //printf ("swap_out success: %p, index: %d, total: %d\n", frame, index, block_size (swap_block));

  return index;
}

bool
swap_in (uint32_t index, void *frame)
{
  //printf("to swap_in: %p\n", frame);
  if (frame == NULL || !is_kernel_vaddr (frame))
    return false;

  ASSERT (index != BITMAP_ERROR && index >= 0 && index < bitmap_size (sectors)
          && index % BLOCKS_PER_PAGE == 0)
  ASSERT(bitmap_test(sectors, index));

  for (int i = 0; i < BLOCKS_PER_PAGE; i++)
  {
    block_read (swap_block, index + i, frame + i * BLOCK_SECTOR_SIZE);
    //printf("index %d complete\n", index + i);
  }

  //printf("read success\n");

  lock_acquire (&lock);
  bitmap_set_multiple (sectors, index, BLOCKS_PER_PAGE, FREE);
  lock_release (&lock);
  return true;
}