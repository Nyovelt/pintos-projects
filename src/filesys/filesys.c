#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/cache.h"
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "threads/thread.h"
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
  /* get filename and path */
  char *directory = (char *) malloc (sizeof (char) * (strlen (name) + 1));
  char *filename = (char *) malloc (sizeof (char) * (strlen (name) + 1));

  bool success = false;
  if (!parse_path (name, directory, filename))
    {
      goto filesys_create_error;
    }


  struct dir *dir = dir_open_path (directory);

  success = (dir != NULL && free_map_allocate (1, &inode_sector)
             && inode_create (inode_sector, initial_size)
             && dir_add (dir, filename, inode_sector));
  if (!success && inode_sector != 0)
    free_map_release (inode_sector, 1);
  dir_close (dir);
filesys_create_error:
  // printf ("success: %d\n", success);
  free (directory);
  free (filename);
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
  // struct dir *dir = dir_open_path (directory);

  struct inode *inode = NULL;
  // printf ("%s:%d, %s\n", __FILE__, __LINE__, name);
  char *directory = (char *) malloc (strlen (name) + 1);
  char *filename = (char *) malloc (strlen (name) + 1);
  if (!parse_path (name, directory, filename))
    {
      free (directory);
      free (filename);
      return NULL;
    }

  struct dir *dir = dir_open_path (directory);
  if (dir == NULL)
    {
      free (directory);
      free (filename);
      return NULL;
    }

  if (strlen (filename) > 0)
    {
      // 如果是文件
      dir_lookup (dir, filename, &inode);
      dir_close (dir);
    }
  else
    {
      // 如果是文件夹
      inode = dir_get_inode (dir);
    }
  if (inode == NULL || inode_is_removed (inode))
    {
      free (directory);
      free (filename);
      return NULL;
    }
  free (filename);
  free (directory);
  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name)
{
  char *directory = (char *) malloc (sizeof (char) * (strlen (name) + 1));
  char *filename = (char *) malloc (sizeof (char) * (strlen (name) + 1));
  bool success = false;
  if (!parse_path (name, directory, filename))
    {

      goto filesys_remove_error;
    }
  struct dir *dir = dir_open_path (directory);
  success = dir != NULL && dir_remove (dir, filename);
  dir_close (dir);
filesys_remove_error:
  free (directory);
  free (filename);
  return success;
}

/* change the working dir */
bool
filesys_chdir (const char *path)
{
  struct dir *dir = dir_open_path (path);
  if (dir == NULL)
    return false;

  /* change cwd */
  dir_close (thread_current ()->cwd);

  thread_current ()->cwd = dir;
  return true;
}

bool
filesys_mkdir (const char *path)
{
  char *directory = (char *) malloc (sizeof (char) * (strlen (path) + 1));
  char *filename = (char *) malloc (sizeof (char) * (strlen (path) + 1));
  bool success = false;
  if (!parse_path (path, directory, filename))
    {
      goto filesys_mkdir_error;
    }
  //printf ("%s:%d, %s, %s, %s \n", __FILE__, __LINE__, path, directory, filename);
  struct dir *dir = dir_open_path (directory);
  block_sector_t inode_sector = 0;
  success = (dir != NULL && free_map_allocate (1, &inode_sector)
             && dir_create (inode_sector, 10)
             && inode_init_dir (inode_open (inode_sector), dir)
             && dir_add (dir, filename, inode_sector));

  if (!success && inode_sector != 0)
    free_map_release (inode_sector, 1);

  dir_close (dir);
filesys_mkdir_error:
  free (directory);
  free (filename);
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
parse_path (const char *path, char *directory, char *name)
{

  // printf ("%s:%d ", __FILE__, __LINE__);
  // printf ("%s\n", path);
  struct inode *inode = NULL;
  char *token, *save_ptr;
  struct inode *prev_inode = NULL;

  *name = "";
  char *ret = directory;

  if (strlen (path) == 0)
    return false;
  char *path_copy = (char *) malloc (strlen (path) + 1);
  memcpy (path_copy, path, strlen (path) + 1);
  if (path[0] == '/')
    {
      *directory = '/';
      directory++;
    }

  //*directory = dir_open_root (); //TODO: 假设先从 root 开始， 后面再改进
  char *tmp = "";
  // int i = 0;
  // printf ("%s:%d,%s,  %s \n", __FILE__, __LINE__, path_copy, token);
  // // return 1;
  // token = strtok_r (path_copy, "/", &save_ptr);
  // printf ("%s:%d,%s,  %s, %s \n", __FILE__, __LINE__, path_copy, token, save_ptr);

  // token = strtok_r (NULL, "/", &save_ptr);


  for (token = strtok_r (path_copy, "/", &save_ptr); token != NULL;
       token = strtok_r (NULL, "/", &save_ptr))
    {

      if (directory && strlen (tmp) > 0)
        {
          memcpy (directory, tmp, strlen (tmp)); // 把上一次的路径加上去
          directory[strlen (tmp)] = '/';
          directory += strlen (tmp) + 1;
        }
      tmp = token;
    }

  *directory = '\0';

  directory = ret;

  memcpy (name, tmp, strlen (tmp) + 1);
  // printf ("%s:%d,%s,  %s \n", __FILE__, __LINE__, directory, name);
  free (path_copy);
  return true;
}
