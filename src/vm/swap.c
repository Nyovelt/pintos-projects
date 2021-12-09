#include <debug.h>
#include <bitmap.h>
#include "devices/block.h"
#include "threads/vaddr.h"
#include "vm/swap.h"
#include "vm/page.h"
#include "vm/frame.h"

#define BLOCKS_PER_PAGE (PGSIZE / BLOCK_SECTOR_SIZE)
#define USED true
#define FREE false

static struct block *swap_block;
static struct bitmap *sectors;

void
swap_init ()
{
  swap_block = block_get_role (BLOCK_SWAP);
  sectors = bitmap_create (block_size (swap_block));
}

uint32_t
swap_out (void *frame)
{
  if (frame == NULL || !is_user_vaddr (frame))
    return -1;

  uint32_t index = bitmap_scan_and_flip (sectors, 0, BLOCK_SECTOR_SIZE, FREE);
  if (index == BITMAP_ERROR)
    return -1;

  for (uint32_t i = 0; i < BLOCKS_PER_PAGE; i++)
    block_write (swap_block, index + i, frame + i * BLOCK_SECTOR_SIZE);

  return index;
}

bool
swap_in (uint32_t index, void *frame)
{
  if (frame == NULL || !is_user_vaddr (frame))
    return false;

  ASSERT (index != BITMAP_ERROR && index >= 0 && index < bitmap_size (sectors) && index % BLOCKS_PER_PAGE == 0)

  for (uint32_t i = 0; i < BLOCKS_PER_PAGE; i++)
    block_read (swap_block, index + i, frame + i * BLOCK_SECTOR_SIZE);

  bitmap_set_multiple (sectors, index, BLOCKS_PER_PAGE, FREE);
  return true;
}