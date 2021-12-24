#include "filesys/cache.h"
#include <hash.h>
#include "devices/block.h"
#include "threads/synch.h"

#define BUF_SIZE 64

struct cache_entry {
	struct hash_elem hash_elem; // for sector_cache_map
	block_sector_t sector; // the sector id of the block occupying the cache entry.
	bool dirty; // for writing back
	bool used; // for clock algorithm
	uint8_t data[BLOCK_SECTOR_SIZE];
	struct rwlock rwlock; // per-entry R/W lock
};

static struct cache_entry cache[BUF_SIZE]; // a statically alloc’d array of 64 blocks
static struct hash sector_cache_map; // a global mapping of sector ids to cache entries
static struct lock hash_lock; // A global lock to guard the hash-map