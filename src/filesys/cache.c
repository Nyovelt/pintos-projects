#include "filesys/cache.h"
#include <hash.h>
#include "devices/block.h"
#include "threads/synch.h"
#include "random.h"
#define BUF_SIZE 64

struct cache_entry
{
    struct hash_elem hash_elem; // for sector_cache_map
    block_sector_t sector;      // the sector id of the block occupying the cache entry.
    bool dirty;                 // for writing back
    bool used;                  // for clock algorithm
    uint8_t data[BLOCK_SECTOR_SIZE];
    struct rwlock rwlock;    // per-entry R/W lock
    struct block *fs_device; // 临时用的，用来连接到 inode 里的 fs_device
};

static struct cache_entry cache[BUF_SIZE]; // a statically alloc’d array of 64 blocks
static struct hash sector_cache_map;       // a global mapping of sector ids to cache entries
static struct lock hash_lock;              // A global lock to guard the hash-map
/* return a hash value of cache_entry e */


// FIXME: this is a hack to get the cache to work. （随机的）
struct cache_entry *
cache_get ()
{
  // 随机找一个倒霉蛋踢掉
  int i
      = random_ulong () % BUF_SIZE;
  if (cache[i].dirty)
    block_write (cache[i].fs_device, cache[i].sector, cache[i].data);
  return cache + i;
}

/* TODO: 
1. 上锁
2. 完善哈希部分
3. 完善时钟算法
*/

struct cache_entry *
cache_find (const block_sector_t sector)
{
  for (int i = 0; i < BUF_SIZE; i++)
    {
      if (cache[i].sector == sector)
        {
          return &cache[i];
        }
    }
  return NULL;
}

void
cache_write (struct block *block, block_sector_t sector, void *buffer)
{
  struct cache_entry *e;
  e = cache_find (sector);
  if (e == NULL)
    {
      /* 
	  当前 sector 不在 cache 中，需要添加到 cache 中
	  初始化，然后塞进去
	  */
      struct cache_entry *e = cache_get ();
      e->sector = sector;
      e->dirty = true;
      e->used = false;
      memcpy (e->data, buffer, BLOCK_SECTOR_SIZE);
      e->fs_device = block;
    }
  else
    {
      memcpy (e->data, buffer, BLOCK_SECTOR_SIZE);
      e->dirty = true;
    }
}

void
cache_read (struct block *block, block_sector_t sector, void *buffer)
{
  struct cache_entry *e;
  e = cache_find (sector);
  if (e == NULL)
    {
      /* 如果要读的部分不在缓存中，那么就直接读内存 
		 TODO: 其实应该加载到缓存中再读
		 */
      block_read (block, sector, buffer);
    }
  else
    {
      memcpy (buffer, e->data, BLOCK_SECTOR_SIZE);
    }
}