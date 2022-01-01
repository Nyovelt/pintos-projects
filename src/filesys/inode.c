#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <stdio.h>
#include <round.h>
#include <string.h>
#include "threads/malloc.h"
#include "threads/synch.h"
#include "filesys/cache.h"
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "stdbool.h"
#include "filesys/directory.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44
#define NUM_DIRECT                                                             \
  123 /* ((BLOCK_SECTOR_SIZE - sizeof (off_t) - sizeof (unsigned)) / sizeof (block_sector_t) - 2) */
#define PTRS_PER_SECTOR                                                        \
  128 /* (BLOCK_SECTOR_SIZE / sizeof (block_sector_t *)) */

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
{
    /* ext2-like design */
    block_sector_t direct[NUM_DIRECT];
    block_sector_t indirect;
    block_sector_t double_indirect;
    //block_sector_t start; /* First data sector. */
    off_t length;   /* File size in bytes. */
    unsigned magic; /* Magic number. */
    //uint32_t unused[125]; /* Not used. */
    bool is_dir;
};

struct indir_block
{
    block_sector_t blocks[PTRS_PER_SECTOR];
};

typedef block_sector_t *indir_block_t;

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode
{
    struct list_elem elem;  /* Element in inode list. */
    block_sector_t sector;  /* Sector number of disk location. */
    int open_cnt;           /* Number of openers. */
    bool removed;           /* True if deleted, false otherwise. */
    int deny_write_cnt;     /* 0: writes ok, >0: deny writes. */
    struct lock ext_lock;   /* For file extenstion */
    struct inode_disk data; /* Inode content. */
};

static bool allocate_direct (off_t, block_sector_t *);
static bool allocate_indirect (off_t, block_sector_t *);
static bool allocate_double_indirect (off_t, block_sector_t *);
static void release_direct (block_sector_t *, off_t);
static void release_indirect (block_sector_t, off_t);
static void release_double_indirect (block_sector_t, off_t);

static bool
ext_free_map_allocate (struct inode_disk *disk_inode)
{
  ASSERT (disk_inode != NULL);
  off_t num_sectors = bytes_to_sectors (disk_inode->length);

  off_t num_direct = (num_sectors < NUM_DIRECT) ? num_sectors : NUM_DIRECT;
  if (!allocate_direct (num_direct, disk_inode->direct))
    return false;

  off_t num_indirect = num_sectors - num_direct;
  num_indirect
      = (num_indirect < PTRS_PER_SECTOR) ? num_indirect : PTRS_PER_SECTOR;
  if (!allocate_indirect (num_indirect, &disk_inode->indirect))
    return false;

  off_t num_double_indirect = num_sectors - num_direct - num_indirect;
  if (!allocate_double_indirect (num_double_indirect,
                                 &disk_inode->double_indirect))
    return false;

  return true;
}

static void
ext_free_map_release (struct inode_disk *disk_inode)
{
  ASSERT (disk_inode != NULL);
  off_t num_sectors = bytes_to_sectors (disk_inode->length);

  off_t num_direct = (num_sectors < NUM_DIRECT) ? num_sectors : NUM_DIRECT;
  //printf ("to dealloc: %d, length=%d\n", num_direct, disk_inode->length);
  release_direct (disk_inode->direct, num_direct);

  off_t num_indirect = num_sectors - num_direct;
  num_indirect
      = (num_indirect < PTRS_PER_SECTOR) ? num_indirect : PTRS_PER_SECTOR;
  release_indirect (disk_inode->indirect, num_indirect);

  off_t num_double_indirect = num_sectors - num_direct - num_indirect;
  release_double_indirect (disk_inode->double_indirect, num_double_indirect);
}

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos)
{
  ASSERT (inode != NULL);
  block_sector_t sector_id = -1;

  if (pos >= inode->data.length)
    return sector_id;
  //return start + pos / BLOCK_SECTOR_SIZE;
  size_t sector_pos = pos / BLOCK_SECTOR_SIZE;

  if (sector_pos < NUM_DIRECT)
    {
      sector_id = inode->data.direct[sector_pos];
      return sector_id;
    }
  else
    {
      sector_pos -= NUM_DIRECT;
      if (sector_pos < PTRS_PER_SECTOR)
        {
          struct indir_block indir;
          cache_read_block (inode->data.indirect, &indir);
          sector_id = indir.blocks[sector_pos];

          return sector_id;
        }
      else
        {
          sector_pos -= PTRS_PER_SECTOR;
          if (sector_pos >= PTRS_PER_SECTOR * PTRS_PER_SECTOR)
            return sector_id;
          ;

          int indir_sector_pos = sector_pos / PTRS_PER_SECTOR;
          int dbl_indir_sector_pos = sector_pos % PTRS_PER_SECTOR;

          struct indir_block dbl_indir;
          cache_read_block (inode->data.double_indirect, &dbl_indir);
          block_sector_t indir_sector_id = dbl_indir.blocks[indir_sector_pos];
          struct indir_block indir;
          cache_read_block (indir_sector_id, &indir);
          sector_id = indir.blocks[dbl_indir_sector_pos];

          return sector_id;
        }
    }
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void)
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      //size_t sectors = bytes_to_sectors (length);
      disk_inode->length = length;
      disk_inode->is_dir = false;
      disk_inode->magic = INODE_MAGIC;
      if (ext_free_map_allocate (
              disk_inode)) //free_map_allocate (sectors, &disk_inode->start))
        {
          //printf("allocated\n");
          cache_write_block (sector, disk_inode);
          /*if (sectors > 0)
            {
              static char zeros[BLOCK_SECTOR_SIZE];
              size_t i;

              for (i = 0; i < sectors; i++)
                cache_write_block (disk_inode->start + i, zeros);
            }*/
          success = true;
        }
      free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e))
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector)
        {
          //printf ("already open\n");
          inode_reopen (inode);

          return inode;
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  // printf ("malloc: %p\n", inode); // DEBUG_INODE
  if (inode == NULL)
    return NULL;
  // printf ("inode_open\n"); // DEBUG_INODE

  /* Initialize. */

  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  //inode->is_dir = false;
  lock_init (&inode->ext_lock);
  cache_read_block (inode->sector, &inode->data);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode)
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */

      list_remove (&inode->elem);

      /* Deallocate blocks if removed. */
      if (inode->removed)
        {
          /*off_t length = disk_inode->length;
          block_sector_t start = disk_inode->start;*/
          //printf("FREE: sector=%d", inode->sector);
          //printf ("release block %d\n", inode->sector);
          free_map_release (inode->sector, 1);
          ext_free_map_release (&inode->data);
          //printf ("release end %d\n", inode->sector);
          /*free_map_release (start,
                            bytes_to_sectors (length));*/
        }
      // printf ("free inode %p\n", inode); // DEBUG_INODE
      free (inode);
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode)
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset)
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  //uint8_t *bounce = NULL;

  if (offset > inode->data.length)
    return 0;

  while (size > 0)
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      cache_read_at (sector_idx, buffer + bytes_read, sector_ofs, chunk_size);

      /*if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          // Read full sector directly into caller's buffer.
          block_read (fs_device, sector_idx, buffer + bytes_read);
        }
      else
        {
          // Read sector into bounce buffer, then partially copy
          // into caller's buffer.
          if (bounce == NULL)
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          block_read (fs_device, sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }*/

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  //free (bounce);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset)
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  if (offset + size > inode->data.length)
    {
      lock_acquire (&inode->ext_lock);
      if (offset + size > (volatile int) inode->data.length)
        {
          off_t prev_length = inode->data.length;
          inode->data.length = offset + size;
          if (!ext_free_map_allocate (&inode->data))
            {
              inode->data.length = prev_length;
              lock_release (&inode->ext_lock);
              return 0;
            }
          cache_write_block (inode->sector, &inode->data);
        }
      lock_release (&inode->ext_lock);
    }

  while (size > 0)
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      /*cache_write_at (sector_idx, buffer + bytes_written, sector_ofs,
                      chunk_size);*/

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          // Write full sector directly to disk.
          cache_write_block (sector_idx, buffer + bytes_written);
        }
      else
        {
          // We need a bounce buffer.
          if (bounce == NULL)
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          // If the sector contains data before or after the chunk
          // we're writing, then we need to read in the sector
          // first.  Otherwise we start with a sector of all zeros.
          if (sector_ofs > 0 || chunk_size < sector_left)
            cache_read_block (sector_idx, bounce);
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          cache_write_block (sector_idx, bounce);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  free (bounce);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode)
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode)
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}

bool
allocate_direct (off_t num_direct, block_sector_t *direct_blocks)
{
  static char zeros[BLOCK_SECTOR_SIZE];
  for (int i = 0; i < num_direct; i++)
    {
      if (direct_blocks[i] == 0)
        {
          if (!free_map_allocate (1, direct_blocks + i))
            {
              //printf ("direct allocate false\n");
              return false;
            }
          // printf ("direct block allocated: %d\n", direct_blocks[i]);
          cache_write_block (direct_blocks[i], zeros);
        }
    }
  return true;
}

bool
allocate_indirect (off_t num_indirect, block_sector_t *indirectp)
{
  if (num_indirect == 0)
    return true;
  if (num_indirect < 0)
    return false;

  struct indir_block *indir = calloc (1, sizeof (struct indir_block));
  if (indir == NULL)
    return false;

  if (*indirectp == 0)
    {
      if (!allocate_direct (1, indirectp))
        {
          free (indir);
          return false;
        }
      // printf ("indirect block allocated: %d\n", *indirectp);
    }
  else
    cache_read_block (*indirectp, indir);

  if (!allocate_direct (num_indirect, indir->blocks))
    {
      free (indir);
      return false;
    }
  cache_write_block (*indirectp, indir);

  free (indir);
  return true;
}

bool
allocate_double_indirect (off_t num_double_indirect,
                          block_sector_t *double_indirectp)
{
  if (num_double_indirect == 0)
    return true;
  if (num_double_indirect < 0
      || num_double_indirect > PTRS_PER_SECTOR * PTRS_PER_SECTOR)
    return false;

  struct indir_block *dbl_indir = calloc (1, sizeof (struct indir_block));
  if (dbl_indir == NULL)
    return false;

  if (*double_indirectp == 0)
    {
      if (!allocate_direct (1, double_indirectp))
        {
          free (dbl_indir);
          return false;
        }
      // printf ("double indirect block allocated: %d\n", *double_indirectp);
    }
  else
    cache_read_block (*double_indirectp, dbl_indir);

  off_t num_indirect = DIV_ROUND_UP (num_double_indirect, PTRS_PER_SECTOR);
  for (int i = 0; i < num_double_indirect; i++)
    {
      off_t num_allocated = (num_double_indirect < PTRS_PER_SECTOR)
                                ? num_double_indirect
                                : PTRS_PER_SECTOR;
      if (!allocate_indirect (num_allocated, dbl_indir->blocks + i))
        {
          free (dbl_indir);
          return false;
        }
      num_indirect -= num_allocated;
    }

  ASSERT (num_indirect == 0);
  cache_write_block (*double_indirectp, dbl_indir);
  free (dbl_indir);
  return true;
}

void
release_direct (block_sector_t *direct_blocks, off_t num_direct)
{
  for (int i = 0; i < num_direct; i++)
    {
      //printf ("begin release direct at %d, sector %d\n", i, direct_blocks[i]);
      ASSERT (direct_blocks[i] != 0)
      free_map_release (direct_blocks[i], 1);
      //printf ("end release direct at %d, sector %d\n", i, direct_blocks[i]);
    }
}

void
release_indirect (block_sector_t indirect, off_t num_indirect)
{
  if (num_indirect <= 0)
    return;

  struct indir_block indir;
  cache_read_block (indirect, &indir);
  //printf("begin release direct from indir\n");
  release_direct (indir.blocks, num_indirect);
  free_map_release (indirect, 1);
}

void
release_double_indirect (block_sector_t double_indirect,
                         off_t num_double_indirect)
{
  if (double_indirect <= 0)
    return;
  ASSERT (num_double_indirect < PTRS_PER_SECTOR * PTRS_PER_SECTOR)

  off_t num_indirect = DIV_ROUND_UP (num_double_indirect, PTRS_PER_SECTOR);
  struct indir_block dbl_indir;
  cache_read_block (double_indirect, &dbl_indir);

  for (int i = 0; i < num_indirect; i++)
    {
      ASSERT (dbl_indir.blocks[i] != 0)
      off_t num_released = (num_double_indirect < PTRS_PER_SECTOR)
                               ? num_double_indirect
                               : PTRS_PER_SECTOR;
      release_indirect (dbl_indir.blocks[i], num_released);
      free_map_release (dbl_indir.blocks[i], 1);
      num_indirect -= num_released;
    }

  free_map_release (double_indirect, 1);
}

bool
inode_is_removed (struct inode *inode)
{
  return inode->removed;
}

bool
inode_is_dir (struct inode *inode)
{
  return inode->data.is_dir;
}

void
inode_set_dir (struct inode *inode)
{
  inode->data.is_dir = true;
  cache_write_block (inode->sector, &inode->data);
}

bool
inode_init_dir (struct inode *inode, struct dir *par_dir)
{

  inode_set_dir (inode);

  struct dir *dir = dir_open (inode);
  bool success = dir_add (dir, ".", inode_get_inumber (inode));
  if (inode_get_inumber (inode) == ROOT_DIR_SECTOR)
    {
      // .. is to self thus
      success = dir_add (dir, "..", inode_get_inumber (inode));
    }
  else
    {
      ASSERT (par_dir != NULL);
      success
          = dir_add (dir, "..", inode_get_inumber (dir_get_inode (par_dir)));
    }
  dir_close (dir);
  return success;
}