#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/cache.h"
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format)
{
  cache_init ();
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();

  if (format)
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void)
{
  cache_writeback ();
  free_map_close ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size)
{
  block_sector_t inode_sector = 0;
  struct dir *dir = dir_open_root ();
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size)
                  && dir_add (dir, name, inode_sector));
  if (!success && inode_sector != 0)
    free_map_release (inode_sector, 1);
  dir_close (dir);

  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  struct dir *dir = dir_open_root ();
  struct inode *inode = NULL;

  if (dir != NULL)
    dir_lookup (dir, name, &inode);
  dir_close (dir);

  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name)
{
  struct dir *dir = dir_open_root ();
  bool success = dir != NULL && dir_remove (dir, name);
  dir_close (dir);

  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}

/* 首先从 root 一层层进行递归， 通过 strtok 来不断的拆文件夹名 */
bool
parse_path (const char *path, char **par_path, char **name)
{
  printf ("%s:%d\n", __FILE__, __LINE__);
  struct inode *inode = NULL;
  char *token, *save_ptr;
  struct inode *prev_inode = NULL;
  *name = "";

  // if (strcmp (path, "/") == 0)
  //   {
  //     strcpy (par_path, "/");
  //     return true;
  //   }

  *par_path = dir_open_root (); //TODO: 假设先从 root 开始， 后面再改进

  for (token = strtok_r (path, "/", &save_ptr); token != NULL; token = strtok_r (NULL, "/", &save_ptr))
    {
      if (strcmp (token, "") == 0)
        continue;

      if (dir_lookup (*par_path, token, &inode) == false)
        {
          dir_close (*par_path);
          printf ("%s:%d, false\n", __FILE__, __LINE__);
          return false;
        }

      if (prev_inode != NULL)
        inode_close (prev_inode);

      prev_inode = inode;
      *par_path = inode;
    }

  return true;
}