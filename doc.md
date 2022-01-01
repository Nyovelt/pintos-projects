             +-------------------------+
             |         CS 140          |
             | PROJECT 4: FILE SYSTEMS |
             |     DESIGN DOCUMENT     |
             +-------------------------+

---- GROUP ----

> Fill in the names and email addresses of your group members.

Yining Zhang <zhangyn@shanghaitech.edu.cn>
Feiran Qin <qinfr@shanghaitech.edu.cn>



---- PRELIMINARIES ----

> If you have any preliminary comments on your submission, notes for the
> TAs, or extra credit, please give them here.

> Please cite any offline or online sources you consulted while
> preparing your submission, other than the Pintos documentation, course
> text, lecture notes, and course staff.

             INDEXED AND EXTENSIBLE FILES
             ============================

---- DATA STRUCTURES ----

> A1: Copy here the declaration of each new or changed `struct' or
> `struct' member, global or static variable, `typedef', or
> enumeration.  Identify the purpose of each in 25 words or less.

```C

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

```
We will use `direct`, `indirect`, `double_indirect` to store the growing file.


> A2: What is the maximum size of a file supported by your inode
> structure?  Show your work.

We have 123 direct blocks, 128 indirect blocks and 128 double indirect blocks. Each block is 512. So the maximum file size is $512 \times (123 + 128 + 128^2) = 193024$ which is about 8.12255859375 MB

---- SYNCHRONIZATION ----

> A3: Explain how your code avoids a race if two processes attempt to
> extend a file at the same time.

//TODO: FIX
In our design, there can be one process write at one block, since we have implemented an r/w lock in `cache.c`. So there won't be a race.

> A4: Suppose processes A and B both have file F open, both
> positioned at end-of-file.  If A reads and B writes F at the same
> time, A may read all, part, or none of what B writes.  However, A
> may not read data other than what B writes, e.g. if B writes
> nonzero data, A is not allowed to see all zeros.  Explain how your
> code avoids this race.

//TODO: 

> A5: Explain how your synchronization design provides "fairness".
> File access is "fair" if readers cannot indefinitely block writers
> or vice versa.  That is, many processes reading from a file cannot
> prevent forever another process from writing the file, and many
> processes writing to a file cannot prevent another process forever
> from reading the file.

//TODO: 

---- RATIONALE ----

> A6: Is your inode structure a multilevel index?  If so, why did you
> choose this particular combination of direct, indirect, and doubly
> indirect blocks?  If not, why did you choose an alternative inode
> structure, and what advantages and disadvantages does your
> structure have, compared to a multilevel index?

Our inode is indexed, compared to continious block or linked blocks, we can offer a medium performance with low overhead and small-file visit performance, large-file visit performance. Which can be fit for most senarios.


                SUBDIRECTORIES
                ==============

---- DATA STRUCTURES ----

> B1: Copy here the declaration of each new or changed `struct' or
> `struct' member, global or static variable, `typedef', or
> enumeration.  Identify the purpose of each in 25 words or less.

```C
struct thread{
    struct dir * cwd; // record the working directory
}
```

---- ALGORITHMS ----

> B2: Describe your code for traversing a user-specified path.  How
> do traversals of absolute and relative paths differ?

If it's an absolute path, which means `path[0] = '\'`, we will start from the root directory, which is `struct dir * dir = open_root_dir();`. If it isn't an absolute path, and the working directory exists, we will start from the current working directory. If it doesn't exist, we will start from the root directory.

My code for traversing is following:
```C
/* if the path is a path, then return the corresponding dir struct */
struct dir *
dir_open_path (const char *path)
{
  // printf ("%s", path);
  /* 首先把 path 拷贝 */
  char *path_copy = malloc (strlen (path) + 1);
  memcpy (path_copy, path, strlen (path) + 1);

  /* 判断是不是是绝对路径 */
  struct dir *dir = dir_open_root ();
  if (path[0] != '/')
    if (thread_current ()->cwd != NULL)
      {
        // /printf ("%s:%d: The CWD works\n", __FILE__, __LINE__);
        dir = dir_reopen (thread_current ()->cwd);
        if (dir == NULL)
          {
            free (path_copy);
            return NULL;
          }
      }
  if (inode_is_removed (dir_get_inode (dir)))
    {
      free (path_copy);
      return NULL;
    }

  /* 递归的打开文件夹 */
  char *save_ptr;
  for (char *token = strtok_r (path_copy, "/", &save_ptr); token != NULL;
       token = strtok_r (NULL, "/", &save_ptr))
    {
      struct dir *chd_dir = NULL;
      struct inode *inode = NULL;

      /* 如果在该文件夹下找不到对应文件/文件夹*/
      if (!dir_lookup (dir, token, &inode))
        {
          dir_close (dir);
          free (path_copy);
          return NULL;
        }

      /* 如果对应文件/文件夹打开失败 */
      chd_dir = dir_open (inode);
      if (chd_dir == NULL)
        {
          dir_close (dir);
          free (path_copy);
          return NULL;
        }

      // 这个时候的 inode 就是下一层文件夹的 ”文件 inode“
      dir = chd_dir;
    }

  free (path_copy);

  return dir;
}
```
Most commits are obvious, I will briefly explain its functions. `strok_r` is a function that split a string by a delimiter, and return the first token. I use it to split `\` and open dir by dir, until the filename left. The filename will be returned, and `dir` will open and return.

---- SYNCHRONIZATION ----

> B4: How do you prevent races on directory entries?  For example,
> only one of two simultaneous attempts to remove a single file
> should succeed, as should only one of two simultaneous attempts to
> create a file with the same name, and so on.

We will check each `remove` and `create` operation. `inode_is_removed` function can help us to check whether the inode is removed or not, useful for `remove`. When create, we will call `lookup`, which will open the block and read the `inode_disk`s, look up in the alliened structure, find its names, and check whether the name is the same as the one we want to create. If it is the same, then we will return `false`, that's how `create` works.

> B5: Does your implementation allow a directory to be removed if it
> is open by a process or if it is in use as a process's current
> working directory?  If so, what happens to that process's future
> file system operations?  If not, how do you prevent it?

It's the `rm_cwd` testcase. We will forbit ant visits from the removed directory, even the `CWD`.
When do `dir_open_path` and traverse, we will check whether the inode is removed or not. When begin from the current working directory, it will also be checked. So, the `CWD` can be deleted.

---- RATIONALE ----

> B6: Explain why you chose to represent the current directory of a
> process the way you did.

I will treate the directory as a **specail** inode, which is a inode with a special type, `is_dir`. So it can be operated as files, and save work time and pattern designs.

                 BUFFER CACHE
                 ============

---- DATA STRUCTURES ----

> C1: Copy here the declaration of each new or changed `struct' or
> `struct' member, global or static variable, `typedef', or
> enumeration.  Identify the purpose of each in 25 words or less.

```C
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
```

---- ALGORITHMS ----

> C2: Describe how your cache replacement algorithm chooses a cache
> block to evict.

> C3: Describe your implementation of write-behind.

> C4: Describe your implementation of read-ahead.

---- SYNCHRONIZATION ----

> C5: When one process is actively reading or writing data in a
> buffer cache block, how are other processes prevented from evicting
> that block?

> C6: During the eviction of a block from the cache, how are other
> processes prevented from attempting to access the block?

---- RATIONALE ----

> C7: Describe a file workload likely to benefit from buffer caching,
> and workloads likely to benefit from read-ahead and write-behind.

               SURVEY QUESTIONS
               ================

Answering these questions is optional, but it will help us improve the
course in future quarters.  Feel free to tell us anything you
want--these questions are just to spur your thoughts.  You may also
choose to respond anonymously in the course evaluations at the end of
the quarter.

> In your opinion, was this assignment, or any one of the three problems
> in it, too easy or too hard?  Did it take too long or too little time?

> Did you find that working on a particular part of the assignment gave
> you greater insight into some aspect of OS design?

> Is there some particular fact or hint we should give students in
> future quarters to help them solve the problems?  Conversely, did you
> find any of our guidance to be misleading?

> Do you have any suggestions for the TAs to more effectively assist
> students in future quarters?

> Any other comments?