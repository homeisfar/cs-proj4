#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "threads/thread.h"
#include "threads/malloc.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
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
  char *filename = calloc (strlen (name) + 1, sizeof (char));
  struct dir *dir = filesys_pathfinder (name, filename);
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, false)
                  && dir_add (dir, filename, inode_sector));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  dir_close (dir);

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
  char *filename = calloc (strlen (name) + 1, sizeof (char));
  struct dir *dir = filesys_pathfinder (name, filename);
  struct inode *inode = NULL; 

  if (dir != NULL)
    dir_lookup (dir, filename, &inode);
  dir_close (dir);
  free (filename);
  
  if (strcmp (name, "/") == 0)
    inode = dir_get_inode (dir_open_root ());

  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  char *filename = calloc (strlen (name) + 1, sizeof (char));
  struct dir *dir = filesys_pathfinder (name, filename);
  bool success = dir != NULL && dir_remove (dir, filename);
  dir_close (dir); 
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

/* Navigates file hierarchy to get to desired directory.
  Returns null pointer if invalid path. */
struct dir *
filesys_pathfinder (const char *name, char *filename)
{
  struct dir *cur_dir;
  struct dir *prev_dir;
  struct inode *cur_inode;

  char *path_dir[96];   // (Arbitrary) number of path files allowed
  int path_len = 0;  // Number of files in path
  int count = 0;
  char *token, *save_ptr;

  char *fn_copy;
  size_t str_len = strlen (name) + 1;
  fn_copy = calloc (str_len, sizeof (char));
  strlcpy (fn_copy, name, str_len);

  if (strcspn (name, "/") == 0)
      cur_dir = dir_open_root ();               // Absolute path
  else 
    {
      if (thread_current ()->cur_dir == NULL)
        cur_dir = dir_open_root ();
      else
        {
          cur_dir = dir_reopen (thread_current ()->cur_dir);     // Relative path
        }
    }

  // tokenize fn_copy
  for (token = strtok_r (fn_copy, "/", &save_ptr);
    token != NULL; token = strtok_r (NULL, "/", &save_ptr))
    path_dir[path_len++] = token;  

  // Loops over path files until reaches invalid dir or end of path
  while ((cur_dir != NULL) && (count < path_len - 1))
    {
      if (!dir_lookup (cur_dir, path_dir[count], &cur_inode))
        return NULL;
      prev_dir = cur_dir;
      cur_dir = dir_open (cur_inode);
      dir_close (prev_dir);
      count++;
    }

  if (path_len == 0 || (strcmp (name, "/") == 0))
    strlcpy (filename, name, str_len);
  else
    strlcpy (filename, path_dir[count], strlen (path_dir[count]) + 1);

// PANIC ("%s %i", name, (strcmp (name, "/") == 0));
  free (fn_copy);
  return cur_dir; 
}
