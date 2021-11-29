#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"
#include "threads/thread.h"


/* Creates a directory with space for ENTRY_CNT entries in the
   given SECTOR.  Returns true if successful, false on failure. */
bool
dir_create (block_sector_t sector, size_t entry_cnt, block_sector_t prev_sector)
{
  return inode_create (sector, entry_cnt * sizeof (struct dir_entry), prev_sector, true); 
  //inode_create(1,16*20)
  //-> block 1에 inode_disk 작성하고 넣음
  //-> block 3에 dir_Entry용 그거 만듦 ㅇㅇ (얘는 bitmap)
}

/* Opens and returns the directory for the given INODE, of which
   it takes ownership.  Returns a null pointer on failure. */
struct dir *
dir_open (struct inode *inode) 
{
  struct dir *dir = calloc (1, sizeof *dir);
  if (inode != NULL && dir != NULL)
    {
      dir->inode = inode;
      dir->pos = inode->data.pos;
      return dir;
    }
  else
    {
      inode_close (inode);
      free (dir);
      return NULL; 
    }
}

/* Opens the root directory and returns a directory for it.
   Return true if successful, false on failure. */
struct dir *
dir_open_root (void)
{
  return dir_open (inode_open (ROOT_DIR_SECTOR));
  //inode가 생성이되고 얘는 open_inode list에 들어가고
  // inode ->sector ==1
  //inode -> data == root inode disk
}

/* Opens and returns a new directory for the same inode as DIR.
   Returns a null pointer on failure. */
struct dir *
dir_reopen (struct dir *dir) 
{
  return dir_open (inode_reopen (dir->inode));
}

/* Destroys DIR and frees associated resources. */
void
dir_close (struct dir *dir) 
{
  if (dir != NULL)
    {
      inode_close (dir->inode);
      free (dir);
    }
}

/* Returns the inode encapsulated by DIR. */
struct inode *
dir_get_inode (struct dir *dir) 
{
  return dir->inode;
}

/* Searches DIR for a file with the given NAME.
   If successful, returns true, sets *EP to the directory entry
   if EP is non-null, and sets *OFSP to the byte offset of the
   directory entry if OFSP is non-null.
   otherwise, returns false and ignores EP and OFSP. */
static bool
lookup (const struct dir *dir, const char *name,
        struct dir_entry *ep, off_t *ofsp) 
//6번 block(root dir entries 들이 적혀 있음)
// in-use(open) & name이 같으면 return true
//(true면 ep에 그 dir_entry와 ofsp에 6번 block에서의 entry 저장)
// else false
{
  struct dir_entry e;
  size_t ofs;
  
  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    //printf("inode_sector %d, name %s, in_use %d\n",e.inode_sector, e.name,e.in_use);
    if (e.in_use && !strcmp (name, e.name)) //열려있고, 이름이 같을 때 
      //strcmp : 같으면 0 반환
      {
        if (ep != NULL)
          *ep = e;
        if (ofsp != NULL)
          *ofsp = ofs;
        return true;
      }
  return false;
}

/* Searches DIR for a file with the given NAME
   and returns true if one exists, false otherwise.
   On success, sets *INODE to an inode for the file, otherwise to
   a null pointer.  The caller must close *INODE. */
bool
dir_lookup (const struct dir *dir, const char *name,
            struct inode **inode)  
{
  struct dir_entry e;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  if (lookup (dir, name, &e, NULL))
    *inode = inode_open (e.inode_sector);
  else
    *inode = NULL;

  return *inode != NULL;
}

bool is_empty_dir(struct dir *dir)
{
  struct dir_entry e;
  size_t ofs;
  int cnt = 0;
  
  ASSERT (dir != NULL);

  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
  {
    if (e.in_use) cnt++;
  }

  if(cnt==2) return true;
  else return false;
}

char *find_filename(struct dir *dir, block_sector_t sector)
{
  struct dir_entry e;
  size_t ofs;
  char *result = NULL;
  
  ASSERT (dir != NULL);

  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
  {
    if (e.in_use && e.inode_sector == sector)
    {
      strlcpy (result, e.name, sizeof e.name);
      break;
    }
  }

  return result;
}


bool not_cur_directory(struct dir *dir)
{
  struct list_elem *e;
  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      if(t->cur_dir_sector == dir->inode->sector) return false;
    }
  return true;
}

/* Adds a file named NAME to DIR, which must not already contain a
   file by that name.  The file's inode is in sector
   INODE_SECTOR.
   Returns true if successful, false on failure.
   Fails if NAME is invalid (i.e. too long) or a disk or memory
   error occurs. */
bool
dir_add (struct dir *dir, const char *name, block_sector_t inode_sector)
{
  struct dir_entry e;
  off_t ofs;
  bool success = false;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Check NAME for validity. */
  if (*name == '\0' || strlen (name) > NAME_MAX)
    return false;

  /* Check that NAME is not in use. */
  if (lookup (dir, name, NULL, NULL)) //이미 dir entries가 있으니까 return false
    goto done;

  
  //struct inode_disk *inode_disk = malloc(sizeof(struct inode_disk));
  //block_read(fs_device, 1, inode_disk);
  //printf("This is dir_inode_disk(1) : ");
  //printf("Information : length %d, direct_blocks[0] %d direct_blocks[1] %d\n",
     //       inode_disk->length, inode_disk->direct_blocks[0],inode_disk->direct_blocks[1]);
  /* Set OFS to offset of free slot.
     If there are no free slots, then it will be set to the
     current end-of-file.
     
     inode_read_at() will only return a short read at end of file.
     Otherwise, we'd need to verify that we didn't get a short
     read due to something intermittent such as low memory. */
  //printf("%d \n",inode_read_at (dir->inode, &e, sizeof e, 300));
  //printf("%d \n",inode_read_at (dir->inode, &e, sizeof e, 320));
  bool flag = false;

  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e){
    //printf("inode_sector %d, name %s, in_use %d\n",e.inode_sector, e.name,e.in_use); 
    if (!e.in_use){
      flag = true;
      break;
    }
  }
  //사용하지 않는 entry면 재활용함!(512개 개수제한 있으니까)
  /* Write slot. */
  if(flag){
    e.in_use = true;
    strlcpy (e.name, name, sizeof e.name);
    e.inode_sector = inode_sector;
    //printf("inode_sector %d, name %s, in_use %d\n",e.inode_sector, e.name,e.in_use); 
    success = inode_write_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
  }

  else
  {
    //entry 확장
    struct dir_entry *f = malloc(sizeof(struct dir_entry));
    f->in_use = true;
    strlcpy (f->name, name, sizeof f->name);
    f->inode_sector = inode_sector;
    success = inode_write_at(dir->inode, f, sizeof e, dir->inode->data.length) == sizeof e;
    free(f);
  }
  

 done:
  //printf("success : %d\n",success);
  //for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
    //   ofs += sizeof e){
    //printf("inode_sector %d, name %s, in_use %d\n",e.inode_sector, e.name,e.in_use); 
 // }
  return success;
}

/* Removes any entry for NAME in DIR.
   Returns true if successful, false on failure,
   which occurs only if there is no file with the given NAME. */
bool
dir_remove (struct dir *dir, const char *name) 
{
  struct dir_entry e;
  struct inode *inode = NULL;
  bool success = false;
  off_t ofs;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Find directory entry. */
  if (!lookup (dir, name, &e, &ofs)) //찾고 있으면 e에 dir_entry저장 ofs에 거기서의 ofs 저장.
    //못찾으면 걍 done
    goto done;

  /* Open inode. */
  inode = inode_open (e.inode_sector);
  if (inode == NULL)
    goto done;

  //if(inode->open_cnt > 1) goto done;

  /* Erase directory entry. */ //아예없애는게 아니라 in_use를 false로 해두는 느낌이구나
  e.in_use = false;
  if (inode_write_at (dir->inode, &e, sizeof e, ofs) != sizeof e) 
    goto done;

  /* Remove inode. */
  //해서 free하게 (일반적으로 write_at 성공하면)
  inode_remove (inode);
  success = true;

 done:
  inode_close (inode);
  return success;
}

/* Reads the next directory entry in DIR and stores the name in
   NAME.  Returns true if successful, false if the directory
   contains no more entries. */
bool
dir_readdir (struct dir *dir, char name[NAME_MAX + 1])
{
  //printf("enter readdir\n");
  struct dir_entry e;

 /* off_t ofs = 0;
  while (inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e) 
    {
      ofs += sizeof e;
      if (e.in_use)
        {
          printf("name is while1 %s\n", e.name);
        } 
    }*/

  while (inode_read_at (dir->inode, &e, sizeof e, dir->pos+ 40 ) == sizeof e) 
    {
      dir->pos += sizeof e;
      if (e.in_use)
        { 
          strlcpy (name, e.name, NAME_MAX + 1);
          dir->inode->data.pos = dir->pos;
       //  printf("name is while %s\n", name);
          return true;
        } 
    }
  dir->inode->data.pos = dir->pos;
  return false;
}