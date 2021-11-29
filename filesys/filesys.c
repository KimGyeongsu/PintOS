#include "filesys/filesys.h"
#include "bitmap.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/cache.h"
#include "filesys/parse.h"
#include "threads/thread.h"



/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

static struct lock create_lock;
static struct lock remove_lock;
/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  //printf("\nFILESYS_INIT START\n");
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  //printf("free_map %x",free_map);
  free_map_init ();
   // printf("free_map_file %x\n",free_map_file);
  //printf("free_map %x",free_map);
  //printf("After free_map_init() : first false index is %d\n", bitmap_scan(free_map, 0,1,false));

  if (format) {
    //printf("DO_FORMAT_START\n");
    do_format ();

    //printf("DO_FORMAT_DONE\n");
  }

  //printf("free_map : first false index is %d\n", bitmap_scan(free_map, 0,1,false));
  //0 : free_map inode disk
  //1 : directory inode disk
  //2 : free_map
  // 3 : 
  free_map_open ();
  //0~6까지 block 할당, 6은 NULL로 설정
  // 열린 file : free_map_file
  // 열린 inode : free_map의 inode (inode ->sector = 0)
  // free_map은 읽어옴.
  //printf("filesys init done\n");
  //printf("free_map : first false index is %d\n", bitmap_scan(free_map, 0,1,false));
  thread_current()->cur_dir_sector = 1;
  lock_init(&create_lock);
  lock_init(&remove_lock);
  write_back_peoridically();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  flush_and_free_cache_table();
  free_map_close ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) 
{
  lock_acquire(&create_lock);
  char *temp = malloc(strlen(name)+1);
  strlcpy(temp, name, strlen(name)+1);
  struct parsing_result *result = parse(temp ,true); //name must not be . or ..

//printf("valid %d filename %s parent directory sector %d is_Dir %d is_root %d\n",
  //          result->valid, result->filename, result->dir_inode->sector, result->is_dir, result->is_root);
  if(result->valid==false) 
  {

    free(result);
    free(temp);
    lock_release(&create_lock);
    return false;
  }
  if(result->is_root ==true){ //case(1) root directory
    free(result);
    free(temp);
    lock_release(&create_lock);
    return false;
  }

  block_sector_t inode_sector = 0;
  struct dir *dir = dir_open(result->dir_inode);
  bool success;

  if(result->is_dir==true) //case(2) other directory
  {
    dir_close(dir);
    free(result);
    free(temp);
    lock_release(&create_lock);
    return false;
  }
  else //case (3) this is file
  {
    success = (dir != NULL
                  && free_map_allocate (1, &inode_sector) 
                  && inode_create (inode_sector, initial_size, dir->inode->sector, false)
                  && dir_add (dir, result->filename, inode_sector));
  }
  if (!success && inode_sector != 0)
  {
    free_map_release (inode_sector, 1);
  }
  dir_close (dir);
  free(result);
  free(temp);
  lock_release(&create_lock);
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
  char *temp = malloc(strlen(name)+1);
  strlcpy(temp, name, strlen(name)+1);
  struct parsing_result *result = parse(temp, false);

  if(result->valid==false) 
  {
    free(result);
    free(temp);
    return false;
  }
  if(result->is_root ==true){ //case(1) root directory
    free(result);
    free(temp);
    return file_open(inode_open(1));
  }
  //case(2) other directory //case (3) other files .  ..
  struct dir *dir = dir_open(result->dir_inode);
  struct inode *inode = NULL;
    //printf("FILESYS_OPEN : name is %s ", name);
  //printf("aaa\n");
  if (dir != NULL)
    dir_lookup (dir, result->filename, &inode);
  dir_close (dir);
    //printf("inode address : %x\n", inode);
  free(result);
  free(temp);
  //printf("go to file open\n");
  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  lock_acquire(&remove_lock);
  char *temp = malloc(strlen(name)+1);
  strlcpy(temp, name, strlen(name)+1);
  struct parsing_result *result = parse(temp, false); 

  if(result->valid==false)
  {
    free(result);
    free(temp);
    lock_release(&remove_lock);
    return false;
  }
  if(result->is_root ==true){ //case(1) root directory
    free(result);
    free(temp);
    lock_release(&remove_lock);
    return false;
  }

  struct dir *dir;
  struct dir *child_dir;
  bool success;
  bool success0;
  struct inode *inode = NULL;
  block_sector_t sector;
  char *result_name;

  if(result->is_dir==false) //case(2) it is a file
  {
    dir = dir_open(result->dir_inode);
    success = dir != NULL && dir_remove (dir, result->filename);
    dir_close (dir); 
  }
  else //case (5) normal directory case ex : ...../a
  {
    dir = dir_open(result->dir_inode);
    if(dir_lookup(dir, result->filename, &inode)!=true) PANIC("ERROR\n");
    child_dir = dir_open(inode);

    if(is_empty_dir(child_dir) && not_cur_directory(child_dir) && inode->open_cnt == 1) //a가 가리키는 child directory : empty or not using by current thread-> remove
    {
      dir_close(child_dir);
      success = dir != NULL && dir_remove (dir, result->filename);
      dir_close(dir);
    }
    else //if not ->do not remove
    {
      dir_close(child_dir);
      dir_close(dir);
      free(result);
      free(temp);
      lock_release(&remove_lock);
      return false;
    }
  }
  free(result);
  free(temp);
  lock_release(&remove_lock);
  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
 // printf ("Formatting file system...");
  //printf("free_map_create_start\n");
   //printf("file_map %x\n",free_map);
  free_map_create ();
   //printf("file_map %x\n",free_map);
  //printf("free_map_create_done\n");
  // 현재 열린 file : free_map_file
  // 현재의 open inode : free_map_file -> inode
  // inode->data 는 : block device의 0에 저장되어 있고, start  : 2
  // 2까지 free_map(bitmap) 저장됨.
  // free_map은 0~2까지만 true인 상태
  //printf("dir create start\n");
  //printf("file_map %x\n",free_map);
  if (!dir_create (ROOT_DIR_SECTOR, 16, 0)) //ROOT_DIR_SECTOR : 1///////////START
    //free_map 0~3까지 true인거고고
    // block index 1 에 root_dir_inode_disk가 할당이되고
    // block index 3 에 0으로 채워진거네(할당은되거고)
    PANIC ("root directory creation failed");

  struct dir * dir = dir_open_root();
  dir_add (dir, ".", 1);
  dir_add (dir, "..",1);
  dir_close(dir);
  //printf("must be 4 first time : First false index is %d\n", bitmap_scan(free_map, 0,1,false));
  //printf("dircreate done\n");
  //printf("free_map_close_start\n");
  free_map_close ();
  //printf("must be 4 remained : First false index is %d\n", bitmap_scan(free_map, 0,1,false));
  //printf("free_map_close_done\n");
  //printf("First false index is %d\n", bitmap_scan(free_map, 0,1,false));
  // open_inode에서 free_map_file ->inode 빼주고
  // free_map_file free해주고
  printf ("done.\n");
}
