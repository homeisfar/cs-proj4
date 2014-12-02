#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44
#define DIRECTNUM 123

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    block_sector_t start;               /* First data sector. */
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    block_sector_t direct[DIRECTNUM];         
    block_sector_t indirect;
    block_sector_t d_indirect;
  };

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
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */
    // block_sector_t *indirect;
    // block_sector_t **d_indirect;
  };

off_t boundary_sectors (struct inode_disk *, off_t);
static block_sector_t extend_by_one (struct inode_disk *);
void release_sectors (struct inode *);

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos)
{
//return -1;

  ASSERT (inode != NULL);
  off_t indirect_size = 128;

  if (pos < inode->data.length)
    {
      off_t index = pos / BLOCK_SECTOR_SIZE;
      off_t offset_index = index - 1;
      if (index == 0)
        {
          return inode->data.start;
        }
      else if (offset_index < DIRECTNUM)
        {
          return inode->data.direct[offset_index];
        }
      else if (offset_index < (DIRECTNUM + indirect_size))
        {
          block_sector_t ret[128];
          // COMMENT: Reading contents of indirect sector into ret buffer
          block_read (fs_device, inode->data.indirect, &ret);                    // COMMENT: Obtaining desired sector
          // COMMENT: Obtaining desired sector
          return ret[offset_index - DIRECTNUM];
        }
      else
        {
          block_sector_t first_indir[128];
          block_sector_t second_indir[128];
          off_t dbl_indirect = (offset_index - (DIRECTNUM + indirect_size)) / 128;
          off_t dbl_index = (offset_index - (DIRECTNUM + indirect_size)) % 128;

          // COMMENT: Reading contents of outer indirect sector into
          // first_indir buffer
          block_read (fs_device, inode->data.d_indirect, &first_indir);
          // COMMENT: Reading contents of a indirect sector into second_indir
          block_read (fs_device, first_indir[dbl_indirect], &second_indir);
          // COMMENT: Obtaining desired sector from inner indirect sector
          return second_indir[dbl_index];
        }
    }
  else
    return -1;
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
        // return false;

  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);
  size_t sectors = bytes_to_sectors (length);
  // Preliminary check for enough filesys space;
  // Does not cover all cases (ex. new indirect block)
  ASSERT (free_map_count (sectors));

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;

      if (free_map_allocate (1, &disk_inode->start)) 
        {
          block_write (fs_device, sector, disk_inode);
          if (sectors > 0) 
            {
              static char zeros[BLOCK_SECTOR_SIZE];
              size_t i = 0;
              block_sector_t alloc_sector;

              // Adding extension capability
              while (i < sectors) 
                {
            		  // Allocate indirect pointer blocks as needed
            		  if (boundary_sectors (disk_inode, BLOCK_SECTOR_SIZE) == -1)
            		    {
            		      PANIC ("Not enough filesys space!\n");
            		    }
            		  // Obtain free space
                  alloc_sector = extend_by_one (disk_inode);
            		  // If failed, exit
            		  if (alloc_sector == -1)
            		    {
            		      PANIC ("Not enough filesys space!\n");
            		    }
            		  // Write to newly allocated sector
                  block_write (fs_device, alloc_sector, zeros);
        		      i++;
        		    }
              block_write (fs_device, sector, disk_inode); 
            }
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
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  block_read (fs_device, inode->sector, &inode->data);
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

/* Closes INODE and writes it to disk. (Does it?  Check code.)
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
          release_sectors (inode);
          free_map_release (inode->sector, 1);
          // free_map_release (inode->data.start,
          //                   bytes_to_sectors (inode->data.length)); 
        }

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
// Not currently sure how this one changes, since byte_to_sector gives the
// sector number for each read.
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

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

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */
          block_read (fs_device, sector_idx, buffer + bytes_read);
        }
      else 
        {
          /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          block_read (fs_device, sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
// What if write hits end of filesys space?
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      // File growth; Desired sector not allocated yet
      while (sector_idx == -1)
      	{
      	  block_sector_t alloc_sector;
      	  static char zeros[BLOCK_SECTOR_SIZE];

      	  if (boundary_sectors (&inode->data, BLOCK_SECTOR_SIZE) == -1)
      	    return bytes_written;

      	  if ((alloc_sector = extend_by_one (&inode->data)) == -1)
      	    return bytes_written;

      	  // Zero out new block sector
      	  // TODO: How to prevent user from seeing non-user written zeroes
      	  block_write (fs_device, alloc_sector, zeros);
          inode->data.length = inode->data.length + 512;

      	  // If allocated sector contains offset, returns valid sector #;
      	  // Otherwise, loop until valid number granted.
      	  sector_idx = byte_to_sector (inode, offset);
      	}

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      // With file extension allowed, bytes left in inode is not useful here
      //off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      //int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < sector_left ? size : sector_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full sector directly to disk. */
          block_write (fs_device, sector_idx, buffer + bytes_written);
        }
      else 
        {
          /* We need a bounce buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
          if (sector_ofs > 0 || chunk_size < sector_left) 
            block_read (fs_device, sector_idx, bounce);
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          block_write (fs_device, sector_idx, bounce);
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

// Below functions assume inode->data.length is length of current file, not
// intended length of file.

/* Allocates indirect pointer block(s) if extending file by given size will
   need it. Returns -1 on failure. Does not allocate data blocks! */
int
boundary_sectors (struct inode_disk *inode, off_t size)
{
  // struct inode_disk *inode = 
  off_t direct_pointers = DIRECTNUM;  // inode->data.start included
  off_t in_direct_ptrs = direct_pointers + 128;
  off_t doubly_indirect = 128;
  off_t cur_alloc = inode->length / BLOCK_SECTOR_SIZE;
  off_t fut_alloc = (inode->length + size) / BLOCK_SECTOR_SIZE;

  // If no new block needs to be allocated for pointers, exit
  if (fut_alloc - cur_alloc == 0)
    return 0;
  // Add one block sector if entering indirect pointers
  else if (cur_alloc < direct_pointers && fut_alloc >= direct_pointers)
    {
      block_sector_t alloc_sec;
      if (!free_map_count (1) || !free_map_allocate (1, &alloc_sec))
        return -1;
      inode->indirect = alloc_sec;
    }
  // Add two block sectors if entering doubly indirect pointers
  else if (cur_alloc < in_direct_ptrs && fut_alloc >= in_direct_ptrs)
    {
      // Allocate buffer to write second sector number into
      block_sector_t d_indirect_buf[128] = {0};

      block_sector_t first_indirect;
      block_sector_t second_indirect;
      if (!free_map_count (2) || !free_map_allocate (1, &first_indirect)
      	  || !free_map_allocate (1, &second_indirect))
      	return -1;
      // Assign first indirection block to inode
      inode->d_indirect = first_indirect;
      d_indirect_buf[0] = second_indirect;
      // Write location of second indirection block into first block
      block_write (fs_device, inode->d_indirect, d_indirect_buf[128]);
    }
  // Add doubly indirect block; Write # to first indirection block
  else if (cur_alloc > in_direct_ptrs && 
	   ((fut_alloc - in_direct_ptrs) % doubly_indirect == 0))
    {
      block_sector_t second_indirect;
      if (!free_map_count (1) || !free_map_allocate (1, &second_indirect))
        return -1;
      /* We need a bounce buffer to hold data already in sector. */
      block_sector_t bounce[128] = {0};
      block_read (fs_device, inode->d_indirect, bounce);
      off_t dbl_idx = (fut_alloc - in_direct_ptrs) / doubly_indirect;
      // Write location of new doubly indirect block into first block
      bounce[dbl_idx] = second_indirect;
      block_write (fs_device, inode->d_indirect, bounce);
    }
  return 0;
}

/* Returns a single allocated block sector number if successful; returns 
   -1 on failure. Currently does not consider cases where there is space
   remaining in an already-allocated inode. Does not increase recorded inode
   length. Excludes cases covered by boundary_sector. */
static block_sector_t
extend_by_one (struct inode_disk *inode)
{
  block_sector_t alloc_sec;
  off_t next_idx = inode->length / 512 - 1; // minus 1 to account for .start
  off_t direct = DIRECTNUM;
  off_t indirect = direct + 128;

  // Checks & allocates if enough filesys space present for extension
  if (!free_map_count (1) || !free_map_allocate (1, &alloc_sec))
    return -1;

  if (next_idx < direct)
    inode->direct[next_idx] = alloc_sec;
  else if (next_idx < indirect)
    {
      block_sector_t bounce[128] = {0};
      block_read (fs_device, inode->indirect, bounce);
      // Write location of new doubly indirect block into first block
      bounce[next_idx - direct] = alloc_sec;
      block_write (fs_device, inode->indirect, bounce);
    }
  else
    {      
      block_sector_t dbl_out_idx[128] = {0};
      block_sector_t dbl_inn_idx[128] = {0};
      off_t outer_index = (next_idx - indirect) / 128;
      off_t inner_indirect = (next_idx - indirect) % 128;

      // Get inner indirection block sector #
      block_read (fs_device, inode->d_indirect, dbl_out_idx);
      block_read (fs_device, dbl_out_idx[outer_index], dbl_inn_idx);
      // Write to inner indirection block
      dbl_inn_idx[inner_indirect] = alloc_sec;
      block_write (fs_device, dbl_out_idx[outer_index], dbl_inn_idx);
    }
  return alloc_sec; 
}


// Used in inode_close to free space and write back to disk
void
release_sectors (struct inode *inode)
{
  block_sector_t dealloc = 0;
  off_t num_blocks = 0;

  // Write back disk_inode struct
  // This inherently includes all direct pointers
  // Also includes the first indirect block sectors
  block_write (fs_device, inode->sector, &inode->data);
  // Deallocate data blocks
  while (dealloc != -1)
    {
      dealloc = byte_to_sector (inode, inode->data.length - 1);
      inode->data.length = inode->data.length - BLOCK_SECTOR_SIZE;
      free_map_release (dealloc, 1);
      num_blocks++;
    }
  PANIC ("number %i", num_blocks);

  // Deallocate indirect pointer blocks
  if (num_blocks >= DIRECTNUM + 1)
    {
      free_map_release (inode->data.indirect, 1);
    }
  // Deallocate doubly indirect pointer blocks
  if (num_blocks >= DIRECTNUM + 129)
    {
      // Indirect blocks
      off_t indir = (num_blocks - (DIRECTNUM + 129)) / 128;
      block_sector_t dbl_indir[128];
      while (indir >= 0)
      	{
      	  block_read (fs_device, inode->data.d_indirect, dbl_indir);
      	  free_map_release (dbl_indir[indir], 1);
      	  indir--;
      	}
      free_map_release (inode->data.d_indirect, 1);
    }
  return;
}

