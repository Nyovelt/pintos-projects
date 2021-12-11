#include "vm/swap.h"
#include <bitmap.h>
#include "devices/block.h"
#include "threads/thread.h"
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
  lock_acquire (&lock);
  if (frame == NULL || !is_kernel_vaddr (frame))
    return -1;

  int index = bitmap_scan_and_flip (sectors, 0, BLOCKS_PER_PAGE, FREE);
  if (index == (int) BITMAP_ERROR)
    return -1;

  for (int i = 0; i < BLOCKS_PER_PAGE; i++)
    block_write (swap_block, index + i, frame + i * BLOCK_SECTOR_SIZE);

  lock_release (&lock);
  return index;
}

bool
swap_in (uint32_t index, void *frame)
{
  lock_acquire (&lock);
  if (frame == NULL || !is_kernel_vaddr (frame))
    return false;

  for (int i = 0; i < BLOCKS_PER_PAGE; i++)
    block_read (swap_block, index + i, frame + i * BLOCK_SECTOR_SIZE);

  bitmap_set_multiple (sectors, index, BLOCKS_PER_PAGE, FREE);
  lock_release (&lock);
  return true;
}

void
swap_free (uint32_t index)
{
  lock_acquire (&lock);
  bitmap_set_multiple (sectors, index, BLOCKS_PER_PAGE, FREE);
  lock_release (&lock);
}