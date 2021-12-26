#include "filesys/cache.h"
#include <debug.h>
#include <stdio.h>
#include <hash.h>
#include <random.h>
#include <string.h>
#include "devices/block.h"
#include "threads/synch.h"
#include "filesys/filesys.h"

#define BUF_SIZE 64

struct cache_entry
{
    struct hash_elem hash_elem; // for sector_cache_map
    block_sector_t sector;      // the sector id of the block occupying the cache entry.
    bool dirty;                 // for writing back
    bool used;                  // for clock algorithm
    uint8_t data[BLOCK_SECTOR_SIZE];
    struct rwlock rwlock; // per-entry R/W lock
};

static struct cache_entry cache[BUF_SIZE]; // a statically alloc’d array of 64 blocks
static int used_slots = 0;
static struct hash sector_cache_map; // a global mapping of sector ids to cache entries
static struct lock global_lock;      // A global lock to guard the hash-map
static int clock_hand = 0;

/* return a hash value of cache_entry e */
static struct cache_entry *cache_find (const block_sector_t);
static struct cache_entry *cache_insert (const block_sector_t);

/* TODO: 
1. 上锁 [Urgent]
2. 完善哈希部分[Delayed]
3. 完善时钟算法[]
*/

void
cache_init ()
{
  lock_init (&global_lock);
  for (int i = 0; i < BUF_SIZE; i++)
    {
      cache[i].sector = -1;
      cache[i].dirty = false;
      cache[i].used = false;
      rwlock_init (&cache[i].rwlock);
    }
}

uint8_t *
cache_access_begin (block_sector_t sector)
{
acquire:
  lock_acquire (&global_lock);
  struct cache_entry *ce = cache_find (sector);
  lock_release (&global_lock);
  if (ce == NULL)
    ce = cache_insert (sector);
  rwlock_begin_read (&ce->rwlock);
  /* Verify that the cache entry still holds the block */
  if (ce->sector != sector)
    {
      rwlock_end_read (&ce->rwlock);
      goto acquire;
    }

  return ce->data;
}

void
cache_access_end (block_sector_t sector)
{
  lock_acquire (&global_lock);
  struct cache_entry *ce = cache_find (sector);
  lock_release (&global_lock);

  ASSERT(ce != NULL);
  ASSERT(ce->sector == sector);
  rwlock_end_read(&ce->rwlock);
}

void
cache_writeback ()
{
  ASSERT (used_slots <= BUF_SIZE);
  for (int i = 0; i < used_slots; i++)
    {
      if (cache[i].dirty)
        {
          block_write (fs_device, cache[i].sector, cache[i].data);
          cache[i].dirty = false;
        }
    }
}

void
cache_write_at (block_sector_t sector, const void *buffer, off_t offset, size_t bytes)
{
acquire:
  lock_acquire (&global_lock);
  struct cache_entry *ce = cache_find (sector);
  lock_release (&global_lock);
  if (ce == NULL)
    ce = cache_insert (sector);
  rwlock_begin_write (&ce->rwlock);
  /* Verify that the cache entry still holds the block */
  if (ce->sector != sector)
    {
      rwlock_end_write (&ce->rwlock);
      goto acquire;
    }

  memcpy (ce->data + offset, buffer, bytes);
  ce->dirty = true;
  rwlock_end_write (&ce->rwlock);
  }

void
cache_read_at (block_sector_t sector, void *buffer, off_t offset, size_t bytes)
{
acquire:
  lock_acquire (&global_lock);
  struct cache_entry *ce = cache_find (sector);
  lock_release (&global_lock);
  if (ce == NULL)
    ce = cache_insert (sector);
  rwlock_begin_read (&ce->rwlock);
  /* Verify that the cache entry still holds the block */
  if (ce->sector != sector)
    {
      rwlock_end_read (&ce->rwlock);
      goto acquire;
    }

  memcpy (buffer, ce->data + offset, bytes);
  rwlock_end_read (&ce->rwlock);
}

/* return a hash value of cache_entry e */
struct cache_entry *
cache_find (const block_sector_t sector)
{
  ASSERT (used_slots <= BUF_SIZE);
  for (int i = 0; i < used_slots; i++)
    {
      if (cache[i].sector == sector)
        {
          cache[i].used = true;
          return &cache[i];
        }
    }
  return NULL;
}

struct cache_entry *
cache_insert (block_sector_t sector)
{
  struct cache_entry *ce = NULL;
  lock_acquire (&global_lock);
  if (used_slots < BUF_SIZE)
    {
      ce = cache + used_slots++;
      lock_release (&global_lock);
      rwlock_begin_write (&ce->rwlock);
    }
  else
    {
      ce = cache + clock_hand;
      int cnt = 0;
      while (ce->used || (cnt < BUF_SIZE && ce->dirty) || !rwlock_try_write (&ce->rwlock))
        {
          ce->used = false;
          clock_hand++;
          clock_hand %= BUF_SIZE;
          ce = cache + clock_hand;
          cnt++;
        }
      lock_release (&global_lock);
      if (ce->dirty)
        {
          block_write (fs_device, ce->sector, ce->data);
          ce->dirty = false;
        }
    }

  ce->sector = sector;
  ce->used = true;
  block_read (fs_device, sector, ce->data);
  rwlock_end_write (&ce->rwlock);
  return ce;
}