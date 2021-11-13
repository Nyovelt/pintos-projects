             +--------------------------+
             |          CS 140          |
             | PROJECT 2: USER PROGRAMS |
             |     DESIGN DOCUMENT      |
             +--------------------------+

---- GROUP ----

> Fill in the names and email addresses of your group members.

Yining Zhang <zhangyn3@shanghaitech.edu.cn>
Feiran Qin <qinfr@shanghaitech.edu.cn>

---- PRELIMINARIES ----

> If you have any preliminary comments on your submission, notes for the
> TAs, or extra credit, please give them here.

> Please cite any offline or online sources you consulted while
> preparing your submission, other than the Pintos documentation, course
> text, lecture notes, and course staff.

               ARGUMENT PASSING
               ================

---- DATA STRUCTURES ----

> A1: Copy here the declaration of each new or changed `struct' or
> `struct' member, global or static variable, `typedef', or
> enumeration.  Identify the purpose of each in 25 words or less.

No `struct` member, global or static variable, `typedef' or enumeration were introduced.

---- ALGORITHMS ----

> A2: Briefly describe how you implemented argument parsing.  How do
> you arrange for the elements of argv[] to be in the right order?
> How do you avoid overflowing the stack page?

```C
  char *argv_[ARGS_LIMIT];
  char *token, *save_ptr;
  int argc = 0;
  for (token = strtok_r (fn_copy, " ", &save_ptr);
       token != NULL;
       token = strtok_r (NULL, " ", &save_ptr))
    argv_[argc++] = token;
  argv_[argc] = NULL;
```

We use the function  `strtok_r()`. If it meets the second argument, which is `"  "` in this case, it will split the string into tokens, and return the first token. Then it will save the pointer of the first token, and use it to split the next token. The process will continue until the string is empty. The last token will be NULL. 

The point is that, for example, first argument is a = ABC0EF0GG, it will return a pointer, point to the a[0], and change a into ABC \0 EF0GG ,then point to a[5], and change a into ABC \0 EF \0 GG. 

Since `fn_copy` is creat by `palloc_get_page (0)`, it will exist until ` palloc_free_page (fn_copy);` is called. That will ensure the memory safe.

And the document says that there has a ARGS_LIMIT of 128. `And palloc_get_page (0)` will return a page with size of 4096 bytes. So it won't overflow the stack page.

Another way to avoid overflow is to use stack_push function, since stack is considerably larger, it has an advantage that  it will not be affected by ARGS_LIMIT. But it will be a little bit complicated. And our way also works fine.

---- RATIONALE ----

> A3: Why does Pintos implement strtok_r() but not strtok()?

`strtok()` is not *thread safe* and `strtok_r()` is the thread-safe alternative.

> A4: In Pintos, the kernel separates commands into a executable name
> and arguments.  In Unix-like systems, the shell does this
> separation.  Identify at least two advantages of the Unix approach.

1. Pipeline
2. The shell can be used to execute commands in a batch file.


                 SYSTEM CALLS
                 ============

---- DATA STRUCTURES ----

> B1: Copy here the declaration of each new or changed `struct' or
> `struct' member, global or static variable, `typedef', or
> enumeration.  Identify the purpose of each in 25 words or less.

In `threads.h`

```
/* Lock used to manipulate files across threads */
struct lock file_lock;

struct thread
  {
    ...
#ifdef USERPROG
    /* Owned by userprog/process.c. */
    uint32_t *pagedir;                  /* Page directory. */
    int exit_code;                      /* Exit code. */

    int next_fd;
    struct list fd_list;
    struct file_descriptor
    {
      struct file *file;
      int fd;
      struct list_elem elem;
    } file_descriptor_;               // record the list of opened files

    /* begin exec */
    struct list child_list;           // eist of child processes
    struct list_elem child_elem;      // elements in the list of child processes
    enum exec_status {
      SUCCESS,
      FAIL,
      WAITING,
      FINISHED
    } load_status;                    // logging child process loads
    struct semaphore sema_load;       // wait for a child process to finish loading
    struct semaphore child_sema_load; // tell child processes to continue execution
    struct semaphore sema_wait;       // wait for a child process to finish running
    struct semaphore child_sema_wait; // tell child processes to continue to exit
    int exit_status;                  // status code returned to the parent when exits
    struct thread *parent;            // identify parent process
    struct file *self;                // identify the execute file
#endif
#endif
  }
```

> B2: Describe how file descriptors are associated with open files.
> Are file descriptors unique within the entire OS or just within a
> single process?

---- ALGORITHMS ----

> B3: Describe your code for reading and writing user data from the
> kernel.

> B4: Suppose a system call causes a full page (4,096 bytes) of data
> to be copied from user space into the kernel.  What is the least
> and the greatest possible number of inspections of the page table
> (e.g. calls to pagedir_get_page()) that might result?  What about
> for a system call that only copies 2 bytes of data?  Is there room
> for improvement in these numbers, and how much?

> B5: Briefly describe your implementation of the "wait" system call
> and how it interacts with process termination.

> B6: Any access to user program memory at a user-specified address
> can fail due to a bad pointer value.  Such accesses must cause the
> process to be terminated.  System calls are fraught with such
> accesses, e.g. a "write" system call requires reading the system
> call number from the user stack, then each of the call's three
> arguments, then an arbitrary amount of user memory, and any of
> these can fail at any point.  This poses a design and
> error-handling problem: how do you best avoid obscuring the primary
> function of code in a morass of error-handling?  Furthermore, when
> an error is detected, how do you ensure that all temporarily
> allocated resources (locks, buffers, etc.) are freed?  In a few
> paragraphs, describe the strategy or strategies you adopted for
> managing these issues.  Give an example.

---- SYNCHRONIZATION ----

> B7: The "exec" system call returns -1 if loading the new executable
> fails, so it cannot return before the new executable has completed
> loading.  How does your code ensure this?  How is the load
> success/failure status passed back to the thread that calls "exec"?

> B8: Consider parent process P with child process C.  How do you
> ensure proper synchronization and avoid race conditions when P
> calls wait(C) before C exits?  After C exits?  How do you ensure
> that all resources are freed in each case?  How about when P
> terminates without waiting, before C exits?  After C exits?  Are
> there any special cases?

---- RATIONALE ----

> B9: Why did you choose to implement access to user memory from the
> kernel in the way that you did?

> B10: What advantages or disadvantages can you see to your design
> for file descriptors?

> B11: The default tid_t to pid_t mapping is the identity mapping.
> If you changed it, what advantages are there to your approach?

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

