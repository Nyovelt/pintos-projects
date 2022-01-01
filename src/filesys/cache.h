#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include "devices/block.h"
#include "filesys/off_t.h"

void cache_init (void);

void cache_write_at (block_sector_t, const void *, off_t offset, size_t bytes);
void cache_write_block (block_sector_t sector, const void *buffer);

void cache_read_at (block_sector_t, void *, off_t offset, size_t bytes);
void cache_read_block (block_sector_t sector, void *buffer);

void cache_writeback (void);

#endif /* filesys/cache.h */
