            +---------------------------+
​            |          CS 140          |
​            | PROJECT 3: VIRTUAL MEMORY |
​            |      DESIGN DOCUMENT      |
​            +---------------------------+

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

            PAGE TABLE MANAGEMENT
            =====================

---- DATA STRUCTURES ----

> A1: Copy here the declaration of each new or changed `struct' or
> `struct' member, global or static variable, `typedef', or
> enumeration.  Identify the purpose of each in 25 words or less.

1. Page: For storing supplemental information about a page.
```C
struct sup_page_table_entry
{
    struct hash_elem hash_elem;
    struct frame_table_entry *frame;
    void *vaddr;   // virtual address of the page
    bool writable; // is the page writable?
    bool is_stack; // is the page in user stack (or in .data/.text)?

    /* For swap */
    int swap_id;

    /* For file operations */
    struct file *file;
    off_t file_ofs;
    uint32_t file_size;

    /* For mmap */
    unsigned hash;
};
```
2. Frame: For storing supplemental information about a frame.
```C
struct frame_table_entry
{
    struct hash_elem hash_elem; // hash_elem for hash_list
    struct lock lock;
    void *kpage;                        // frame pointer
    struct thread *owner;               // owner of the frame
    struct sup_page_table_entry *upage; // auxiliary page entry

    /* For clock algorithm */
    bool used;
    bool dirty;
};
```
3. Thread: For storing threadable supplemental page table, memory map information and other entries.
```C
#ifdef VM
    /* Owned by vm/page.c. */
    struct hash sup_page_table; // 页表
    void *esp;                  // 临时的esp
    struct list mmap_list;      // 保存这个进程所有的  memory map
    mapid_t next_mapid;         // next map id

    struct mmap_descriptor
    {
        struct list_elem elem;
        mapid_t mapid;
        struct file *file;
        void *addr;
        uint32_t file_size;
        struct file_descriptor *fd;
        bool dead;
        int hack;
    } mmap_descriptor;

#endif
```

---- ALGORITHMS ----

> A2: In a few paragraphs, describe your code for accessing the data
> stored in the SPT about a given page.

Normally, the pintos will get trap of `page_fault` if it is accessing a unaccessable page, with an address. We have decleared a function called `page_fault_handler` to handle this situation. In this function, we will check if the page is in the SPT, if it is, we will return the frame pointer of the page, if not, we will allocate a new frame and return the frame pointer. And there are also some techniques about page replacement algorithm, but we decoded not to show it in this answer.

And to look up pages in spt, since we are using `hash_table` to store supplemental table entries, we can use `hash_find` to find the entry. It can be shown in the `page_lookup` function.

> A3: How does your code coordinate accessed and dirty bits between
> kernel and user virtual addresses that alias a single frame, or
> alternatively how do you avoid the issue?

While manipulating these bits we check if the `owner ` of the `frame` is the current `thread`, thus we avoid this issue.

---- SYNCHRONIZATION ----

> A4: When two user processes both need a new frame at the same time,
> how are races avoided?

We incorporate locks into individual frames to coordinate on frames. So we implement pinning as well as block other threads from accessing the `frame`.

---- RATIONALE ----

> A5: Why did you choose the data structure(s) that you did for
> representing virtual-to-physical mappings?

```C
struct sup_page_table_entry
{
    struct hash_elem hash_elem;
    struct frame_table_entry *frame;
    void *vaddr;   // virtual address of the page
    bool writable; // is the page writable?
    bool is_stack; // is the page in user stack (or in .data/.text)?

    /* For swap */
    int swap_id;

    /* For file operations */
    struct file *file;
    off_t file_ofs;
    uint32_t file_size;

    /* For mmap */
    unsigned hash;
};
```

We obtain a map of an address, a frame and other information. By mapping, we can give the corresponding `frame` to `user`/`kernel` when they need to visit it. By bookkeeping, we can handle more kinds of memory visit, such as `file` and `swap`. We choose that structure because it can record everything we need and is convenient for quick checking, it is friendly for programming.


               PAGING TO AND FROM DISK
               =======================

---- DATA STRUCTURES ----

> B1: Copy here the declaration of each new or changed `struct' or
> `struct' member, global or static variable, `typedef', or
> enumeration.  Identify the purpose of each in 25 words or less.

It is the same as our answer to  `A1`. 

1. Page: For storing supplemental information about a page.
```C
struct sup_page_table_entry
{
    struct hash_elem hash_elem;
    struct frame_table_entry *frame;
    void *vaddr;   // virtual address of the page
    bool writable; // is the page writable?
    bool is_stack; // is the page in user stack (or in .data/.text)?

    /* For swap */
    int swap_id;

    /* For file operations */
    struct file *file;
    off_t file_ofs;
    uint32_t file_size;

    /* For mmap */
    unsigned hash;
};
```
2. Frame: For storing supplemental information about a frame.
```C
struct frame_table_entry
{
    struct hash_elem hash_elem; // hash_elem for hash_list
    struct lock lock;
    void *kpage;                        // frame pointer
    struct thread *owner;               // owner of the frame
    struct sup_page_table_entry *upage; // auxiliary page entry

    /* For clock algorithm */
    bool used;
    bool dirty;
};
```
3. Thread: For storing threadable supplemental page table, memory map information and other entries.
```C
#ifdef VM
    /* Owned by vm/page.c. */
    struct hash sup_page_table; // 页表
    void *esp;                  // 临时的esp
    struct list mmap_list;      // 保存这个进程所有的  memory map
    mapid_t next_mapid;         // next map id

    struct mmap_descriptor
    {
        struct list_elem elem;
        mapid_t mapid;
        struct file *file;
        void *addr;
        uint32_t file_size;
        struct file_descriptor *fd;
        bool dead;
        int hack;
    } mmap_descriptor;

#endif
```

---- ALGORITHMS ----

> B2: When a frame is required but none is free, some frame must be
> evicted.  Describe your code for choosing a frame to evict.

Clock algorithm
```C
static void *
frame_evict ()
{
  struct frame_table_entry *fte = NULL;
  struct hash_iterator clock;
  hash_first (&clock, &frame_table);
  while (hash_next (&clock))
    {
      fte = hash_entry (hash_cur (&clock), struct frame_table_entry, hash_elem);
      if (!lock_held_by_current_thread (&fte->lock)
          && !lock_try_acquire (&fte->lock))
        continue;

      /* Check if used */
      if (!fte->used)
        break;
      else
        fte->used = 0;
    }

  int index = swap_out (fte->kpage);
  if (index == -1)
    return NULL;

  fte->upage->swap_id = index; // needed for reclaiming
  fte->upage->frame = NULL;    // unmap upage
  void *ret = fte->kpage;
  lock_release (&fte->lock);
  frame_clear (fte);
  return ret;
}

void
frame_init ()
{
  hash_init (&frame_table, frame_hash, frame_less, NULL);
  lock_init (&lock);
}
```
> B3: When a process P obtains a frame that was previously used by a
> process Q, how do you adjust the page table (and any other data
> structures) to reflect the frame Q no longer has?

As described in **pintos-Guide**,

• You will be evicting the frame, therefore you the page associated with the frame you have selected needs to be unlinked. Then you want to remove this frame from your frame table after you have freed the frame with pagedir_clear_page

• You do not want to delete the supplementary page table entry associated with the selected frame. The process that was using the frame should still have the illusion that they still have this page allocated to them. If you delete this page table entry, you will not be able to reclaim the data from disk when needed.

• Find a free block to write your data to. Since the blocks are just numbered contiguously, you just need an index that is free. Now this index is going to be needed to reclaim the data of the page, therefore it would be best to keep this index of where the data is in some member variable in the supplemental page table entry

• You’ll also want to keep track of which pages are evicted and which are not for quick checking
> B4: Explain your heuristic for deciding whether a page fault for an
> invalid virtual address should cause the stack to be extended into
> the page that faulted.

First the user memory is between `0x8000000` to `0xC000000`, which is the valid zone. Next, the address should above the `esp-32` for it's the zone for stack. So when `page_fault` happens from `esp-32` to `PHYS_BASE`, we will consider it can be a stack growth, which is also mentioned in the **PintOS Guide.**

---- SYNCHRONIZATION ----

> B5: Explain the basics of your VM synchronization design.  In
> particular, explain how it prevents deadlock.  (Refer to the
> textbook for an explanation of the necessary conditions for
> deadlock.)

//TODO: 

> B6: A page fault in process P can cause another process Q's frame
> to be evicted.  How do you ensure that Q cannot access or modify
> the page during the eviction process?  How do you avoid a race
> between P evicting Q's frame and Q faulting the page back in?

the `frame_table_entry` records `  struct sup_page_table_entry *upage;`  and ` struct thread *owner;`so we can prevent this from happening.

> B7: Suppose a page fault in process P causes a page to be read from
> the file system or swap.  How do you ensure that a second process Q
> cannot interfere by e.g. attempting to evict the frame while it is
> still being read in?

//TODO: 

> B8: Explain how you handle access to paged-out pages that occur
> during system calls.  Do you use page faults to bring in pages (as
> in user programs), or do you have a mechanism for "locking" frames
> into physical memory, or do you use some other design?  How do you
> gracefully handle attempted accesses to invalid virtual addresses?

```
exit(-1)
```

---- RATIONALE ----

> B9: A single lock for the whole VM system would make
> synchronization easy, but limit parallelism.  On the other hand,
> using many locks complicates synchronization and raises the
> possibility for deadlock but allows for high parallelism.  Explain
> where your design falls along this continuum and why you chose to
> design it this way.

We use a single lock since pintos has limited support for parallelism.


             MEMORY MAPPED FILES
             ===================

---- DATA STRUCTURES ----

> C1: Copy here the declaration of each new or changed `struct' or
> `struct' member, global or static variable, `typedef', or
> enumeration.  Identify the purpose of each in 25 words or less.

It is the same as our answer to `A1`. But the following is important to MMAP, which records all the existing memory mapped files in `thread.h`

```C
    struct list mmap_list;      // 保存这个进程所有的  memory map
    mapid_t next_mapid;         // next map id

    struct mmap_descriptor
    {
        struct list_elem elem;
        mapid_t mapid;
        struct file *file;
        void *addr;
        uint32_t file_size;
        struct file_descriptor *fd;
        bool dead;
        int hack;
    } mmap_descriptor;
```

---- ALGORITHMS ----

> C2: Describe how memory mapped files integrate into your virtual
> memory subsystem.  Explain how the page fault and eviction
> processes differ between swap pages and other pages.

When `syscall_mmap` is called, we will grab serveral pages from memory by `frame_get`, which establishes a map of physical memory and virtual memory. We will then store them (the frame of file properties) into `sup_page_table_entry` and new a `mmap_descriptor` object in the thread just as `file_descriptor` in Project 2.

TODO: evict and page fault.

> C3: Explain how you determine whether a new file mapping overlaps
> any existing segment.

Here's the code:
```C
  for (int i = 0; i < file_length (f->file); i += PGSIZE)
    {
      if (page_lookup (&thread_current ()->sup_page_table, addr + i) != NULL)
        return -1;
    }
```

Basiclly, we use `page_lookup` to find whether this part of virtual memory overlaps any existing virtual memory. We choose to do it before reading the file because it will be much complicated to handle if we detect overlaps during slicing file memories, which requires roll back operations.



---- RATIONALE ----

> C4: Mappings created with "mmap" have similar semantics to those of
> data demand-paged from executables, except that "mmap" mappings are
> written back to their original files, not to swap.  This implies
> that much of their implementation can be shared.  Explain why your
> implementation either does or does not share much of the code for
> the two situations.

We do not share this part of code because mmap write to file and swap write to memory.

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
> students, either for future quarters or the remaining projects?

> Any other comments?