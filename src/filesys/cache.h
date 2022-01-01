#include "devices/block.h"
#include "filesys/off_t.h"

void cache_init (void);

void cache_write_at (block_sector_t, const void *, off_t offset, size_t bytes);
inline void cache_write_block (block_sector_t sector, const void *buffer)
{
  cache_write_at (sector, buffer, 0, BLOCK_SECTOR_SIZE);
}

void cache_read_at (block_sector_t, void *, off_t offset, size_t bytes);
inline void cache_read_block (block_sector_t sector, void *buffer)
{
  cache_read_at (sector, buffer, 0, BLOCK_SECTOR_SIZE);
}

void cache_writeback(void);