#include "filesys/free-map.h"
#include <bitmap.h>
#include <debug.h>
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "filesys/cache.h"

//static struct file *free_map_file;   /* Free map file. */
//static struct bitmap *free_map;      /* Free map, one bit per sector. */

/* Initializes the free map. */
void
free_map_init (void) 
{
  lock_acquire(&fs_lock);
  free_map = bitmap_create (block_size (fs_device)); //4096 bit -> 512 bytes
  lock_release(&fs_lock);
  if (free_map == NULL)
    PANIC ("bitmap creation failed--file system device is too large");
  bitmap_mark (free_map, FREE_MAP_SECTOR); //define as 0
  //inode block for free_map -> set true : 사용중
  bitmap_mark (free_map, ROOT_DIR_SECTOR); //define as 1
  //inode block for root dir
}

/* Allocates CNT consecutive sectors from the free map and stores
   the first into *SECTORP.
   Returns true if successful, false if not enough consecutive
   sectors were available or if the free_map file could not be
   written. */
bool
free_map_allocate (size_t cnt, block_sector_t *sectorp)
{

  block_sector_t sector = bitmap_scan_and_flip (free_map, 0, cnt, false);
  //0번째(처음부터), 쭉 scan해서, 연속적인 false cnt block을 찾고, 그거의 시작
  //index를 return true로 설정.
  if (sector != BITMAP_ERROR
      && free_map_file != NULL
      && !bitmap_write (free_map, free_map_file))
    //free_map_allocate 해줄 때 마다, free_map 내용이 바뀌니까, 그 내용을 갱신해주
    //는거네
    {
      bitmap_set_multiple (free_map, sector, cnt, false); 
      sector = BITMAP_ERROR;
    }
  if (sector != BITMAP_ERROR)
    *sectorp = sector;
  return sector != BITMAP_ERROR;
}
///////////////////////////////////////////////////////////////////////////EXTENSIBLE
static bool free_map_allocate_direct(size_t block_offset, struct inode_disk * inode_disk, bool flag);
static bool free_map_allocate_indirect(size_t block_offset, struct inode_disk * inode_disk, bool flag);
static bool free_map_allocate_dindirect(size_t block_offset, struct inode_disk * inode_disk, bool flag);

bool
free_map_allocate_extensible (enum status_inode status, size_t block_offset ,
                              struct inode_disk * inode_disk, bool block_write_flag)
{
  bool result;
  lock_acquire(&fs_lock);
  if(status == DIRECT)
  {
    if(block_write_flag){
      result = free_map_allocate_direct(block_offset, inode_disk, true);
      lock_release(&fs_lock);
      return result;
    }
    else{
      result = free_map_allocate_direct(block_offset, inode_disk, false);
      lock_release(&fs_lock);
      return result;
    }
  }
  else if(status == INDIRECT)
  {
    bool direct_alloc = free_map_allocate_direct(121, inode_disk, false);
    if(direct_alloc == false){
      lock_release(&fs_lock);
      return false;
    }
    result = free_map_allocate_indirect(block_offset, inode_disk, true);
    lock_release(&fs_lock);
    return result;
  }
  else if(status == D_DIRECT){
    bool direct_alloc = free_map_allocate_direct(121, inode_disk, false);
    if(direct_alloc == false){
      lock_release(&fs_lock);
      return false;
    }
    bool indirect_alloc = free_map_allocate_indirect(128, inode_disk, false);
    if(indirect_alloc == false){
      lock_release(&fs_lock);
      return false;
    }
    result = free_map_allocate_dindirect(block_offset, inode_disk, true);
    lock_release(&fs_lock);
    return result;
  }
  lock_release(&fs_lock);
  return false;
}


static bool free_map_allocate_direct(size_t block_offset, struct inode_disk * inode_disk, bool flag){
  //printf("fff %x",free_map_file); //103.xx
  //printf("block_offset %d\n",block_offset);
  /////// block_offset 이 0이면
  for(int i=0; i<block_offset; i++)
  {
    block_sector_t sector = bitmap_scan_and_flip(free_map,0,1,false);
    if(sector == -1)
    {
      bitmap_read(free_map,free_map_file);
      return false;   
    }
    inode_disk->direct_blocks[i] = sector;

    static char zeros[BLOCK_SECTOR_SIZE];
    block_write (fs_device, sector, zeros);
  }
  if(flag)  bitmap_write(free_map, free_map_file);
  //printf("must be 109 remained : First false index is %d\n", bitmap_scan(free_map, 0,1,false));
  //printf("inode_disk infor : 103 index %d length %d NULL %d\n",inode_disk->direct_blocks[103], inode_disk->length, inode_disk->direct_blocks[104]);
  return true;
}

//direct block이 설정된 상황에서 indirect block setting
static bool free_map_allocate_indirect(size_t block_offset, struct inode_disk * inode_disk, bool flag){

  /* indirect blocks에 대한 free_map 설정(free_map_file)설정, 
      inode_disk->indirect_block_index 에 index 기록*/
  block_sector_t indirect_block_index = bitmap_scan_and_flip(free_map,0,1,false);
  if(indirect_block_index == -1)
  {
    bitmap_read(free_map,free_map_file);
    return false;   
  }
  inode_disk->indirect_block_index = indirect_block_index;

  /*bounce buffer와 비슷하게 역할을 하는 indirect_block을 malloc해옴. 
    이에 기록함.(indirect_block->block_index에 sector를 저장)*/
  struct indirect_block *indirect_block = malloc(sizeof(struct indirect_block));
  for(int i=0; i<block_offset; i++)
  {
    block_sector_t sector = bitmap_scan_and_flip(free_map,0,1,false);
    if(sector == -1)
    {
      bitmap_read(free_map,free_map_file);
      free(indirect_block);
      return false;   
    }
    indirect_block->block_index[i] = sector;

    static char zeros[BLOCK_SECTOR_SIZE];
    block_write (fs_device, sector, zeros);
  }
  /*free하기 전, 이 indirect_block을 inode_disk->indirect_block_index에 적어야함*/
  block_write(fs_device, indirect_block_index, indirect_block);
  /*free indirect_block*/
  free(indirect_block);

  if(flag)  bitmap_write(free_map, free_map_file);
  return true;
}

static bool free_map_allocate_dindirect(size_t block_offset, struct inode_disk * inode_disk, bool flag){

  int num_of_indirect_block = block_offset / 128;
  int remainder = block_offset % 128;
  /*d_indirect allocation*/
  block_sector_t dindirect_block_index = bitmap_scan_and_flip(free_map,0,1,false);
  if(dindirect_block_index==-1)
  {
    bitmap_read(free_map,free_map_file);
    return false;
  }
  inode_disk->d_indirect_block_index = dindirect_block_index;

  /*d_indirect_block에 실제 메모리 할당*/
  struct d_indirect_block * d_indirect_block = malloc(sizeof(struct d_indirect_block));

  //d_indirect_block의 indirect_block_index[i]에 대해서 free_map_위에 함수 실행
  for(int i=0; i<num_of_indirect_block; i++)
  {
    block_sector_t indirect_block_index = bitmap_scan_and_flip(free_map,0,1,false);
    if(indirect_block_index == -1)
    {
      bitmap_read(free_map,free_map_file);
      free(d_indirect_block);
      return false;   
    }
    d_indirect_block->indirect_block_index[i] = indirect_block_index;
  /*bounce buffer와 비슷하게 역할을 하는 indirect_block을 malloc해옴. 
    이에 기록함.(indirect_block->block_index에 sector를 저장)*/
    struct indirect_block *indirect_block = malloc(sizeof(struct indirect_block));

    for(int j=0; j<128; j++)
    {
      block_sector_t sector = bitmap_scan_and_flip(free_map,0,1,false);
      if(sector == -1)
      {
        bitmap_read(free_map,free_map_file);
        free(d_indirect_block);
        free(indirect_block);
        return false;   
      }
      indirect_block->block_index[j] = sector;

      static char zeros[BLOCK_SECTOR_SIZE];
      block_write (fs_device, sector, zeros);
    }
    /*free하기 전, 이 indirect_block을 inode_disk->indirect_block_index에 적어야함*/
    block_write(fs_device, indirect_block_index, indirect_block);
    /*free indirect_block*/
    free(indirect_block);
  }
  /*For remainder*/
  if(remainder!=0)
  {
    block_sector_t indirect_block_index = bitmap_scan_and_flip(free_map,0,1,false);
    if(indirect_block_index==-1)
    {
      bitmap_read(free_map,free_map_file);
      free(d_indirect_block);
      return false;
    }
    d_indirect_block->indirect_block_index[num_of_indirect_block] = indirect_block_index;
  /*bounce buffer와 비슷하게 역할을 하는 indirect_block을 malloc해옴. 
    이에 기록함.(indirect_block->block_index에 sector를 저장)*/
    struct indirect_block *indirect_block = malloc(sizeof(struct indirect_block));
    for(int i=0; i<remainder; i++)
    {
      block_sector_t sector = bitmap_scan_and_flip(free_map,0,1,false);
      if(sector==-1)
      {
        bitmap_read(free_map,free_map_file);
        free(d_indirect_block);
        free(indirect_block);
        return false;
      }
      indirect_block->block_index[i] = sector;

      static char zeros[BLOCK_SECTOR_SIZE];
      block_write (fs_device, sector, zeros);
    }
    /*free하기 전, 이 indirect_block을 inode_disk->indirect_block_index에 적어야함*/
    block_write(fs_device, indirect_block_index, indirect_block);
    /*free indirect_block*/
    free(indirect_block);
  }
  block_write(fs_device, dindirect_block_index, d_indirect_block);
  /*free indirect_block*/
  free(d_indirect_block);

  if(flag) bitmap_write(free_map, free_map_file);
  return true;
}
/////////////////////////////////////////////////////

/* Makes CNT sectors starting at SECTOR available for use. */
void
free_map_release (block_sector_t sector, size_t cnt)
{
  ASSERT (bitmap_all (free_map, sector, cnt)); //check whether sector~sector+cnt-1 are all true
  bitmap_set_multiple (free_map, sector, cnt, false); //싹다 false로 설정
  bitmap_write (free_map, free_map_file); //free_map_file에 반영
}
///////////////////////////////EXTENSIBLE///////////////////////////////
static void 
free_map_release_direct(size_t block_offset ,struct inode_disk * inode_disk, bool flag);
static void 
free_map_release_indirect(size_t block_offset ,struct inode_disk * inode_disk, bool flag);
static void 
free_map_release_dindirect(size_t block_offset ,struct inode_disk * inode_disk, bool flag);

void
free_map_release_extensible(enum status_inode status,size_t block_offset ,struct inode_disk * inode_disk)
{
  //lock_acquire(&fs_lock);
  if(status == DIRECT)
  {
    free_map_release_direct(block_offset, inode_disk,true);
  }
  else if(status == INDIRECT)
  {
    free_map_release_direct(121, inode_disk,false);
    free_map_release_indirect(block_offset, inode_disk,true);
  }
  else if(status == D_DIRECT){
    free_map_release_direct(121, inode_disk,false);
    free_map_release_indirect(128, inode_disk,true);
    free_map_release_dindirect(block_offset, inode_disk,false);
  }
  //lock_release(&fs_lock);
}

static void 
free_map_release_direct(size_t block_offset ,struct inode_disk * inode_disk,bool flag)
{//block_offset == 0
  for(int i=0; i<block_offset; i++)
  {
     ASSERT (bitmap_all (free_map, inode_disk->direct_blocks[i], 1)); //check that bit is true
     bitmap_set(free_map, inode_disk->direct_blocks[i], false); //make that bit is false
  }
  if(flag)  bitmap_write(free_map, free_map_file);
}

static void 
free_map_release_indirect(size_t block_offset ,struct inode_disk * inode_disk,bool flag)
{
  struct indirect_block *indirect_block = malloc(sizeof(struct indirect_block));
  block_read (fs_device, inode_disk->indirect_block_index, indirect_block);
  for(int i=0; i<block_offset; i++)
  {
    ASSERT (bitmap_all (free_map, indirect_block->block_index[i], 1)); //check that bit is true
    bitmap_set(free_map, indirect_block->block_index[i], false); //make that bit is false
  }
  free(indirect_block);
  if(flag)  bitmap_write(free_map, free_map_file);
}

static void 
free_map_release_dindirect(size_t block_offset ,struct inode_disk * inode_disk,bool flag)
{
  int num_of_indirect_block = block_offset / 128;
  int remainder = block_offset % 128;

  struct d_indirect_block *d_indirect_block = malloc(sizeof(struct d_indirect_block));
  block_read (fs_device, inode_disk->d_indirect_block_index, d_indirect_block);

  for(int i=0; i<num_of_indirect_block; i++){
    struct indirect_block *indirect_block = malloc(sizeof(struct indirect_block));
    block_read (fs_device, d_indirect_block->indirect_block_index[i], indirect_block);
    for(int j=0; j<128; j++){
      ASSERT (bitmap_all (free_map, indirect_block->block_index[j], 1)); //check that bit is true
      bitmap_set(free_map, indirect_block->block_index[j], false); //make that bit is false 
    }
    free(indirect_block);
  }

  struct indirect_block *indirect_block = malloc(sizeof(struct indirect_block));
  block_read (fs_device, d_indirect_block->indirect_block_index[num_of_indirect_block], indirect_block);
  for(int i=0; i<remainder; i++){
    ASSERT (bitmap_all (free_map, indirect_block->block_index[i], 1)); //check that bit is true
    bitmap_set(free_map, indirect_block->block_index[i], false); //make that bit is false 
  }
  free(indirect_block);

  free(d_indirect_block);
  if(flag)  bitmap_write(free_map, free_map_file);
}
//////////////////////////////////////////////////////////////////

/* Opens the free map file and reads it from disk. */
void
free_map_open (void) 
{
  //printf("At free_map_open\n");
  //printf("free_map : first false index is %d\n", bitmap_scan(free_map, 0,1,false));
  struct inode *inode = inode_open (FREE_MAP_SECTOR);
  //printf("inode information : sector %d open_cnt %d removed %d deny_write_cnt %d\n",
   //         inode->sector,inode->open_cnt,inode->removed,inode->deny_write_cnt);
  //struct inode_disk data = inode->data;
 // printf("inode_disk infor : start %d length %d\n",data.start, data.length);
  free_map_file = file_open (inode);
  //printf("free_map : first false index is %d\n", bitmap_scan(free_map, 0,1,false));
  //file_map_file open하고, free_map_file->inode를 설정하는거고
  //inode->sector : 0
  //inode->data : block 0에서 읽어온거 즉. free_map의 inode_disk
  //얘를 open_inode에 넣은거야.
  if (free_map_file == NULL)
    PANIC ("can't open free map");
 // printf("goto BITMAP_READ\n");
  //printf("bitmap_read_at free_map open\n");
  //printf("first false index is %d\n", bitmap_scan(free_map, 0,1,false));
  if (!bitmap_read (free_map, free_map_file))
    //freemap 읽어오기
    PANIC ("can't read free map");
//printf("first false index is %d\n", bitmap_scan(free_map, 0,1,false));
 // printf("bitmap_read_done at free_map open\n");
}

/* Writes the free map to disk and closes the free map file. */
void
free_map_close (void) 
{
  file_close (free_map_file);
}

/* Creates a new free map file on disk and writes the free map to
   it. */
void
free_map_create (void) 
{
  /* Create inode. */
  if (!inode_create (FREE_MAP_SECTOR, bitmap_file_size (free_map), -1, false))
    //bitmap_file_size : bitmap의 bit의 개수 -> 를 file _size로 사용하려고
    // fs_device의 blocksize가 되는거네
    //FREE_MAP_SECTOR : 0
    PANIC ("free map creation failed");

  //printf("after inode create at free_map_create, first false index is %d\n",bitmap_scan(free_map, 0,1,false)); //must be 3처음으로 바뀌어야 함
  //printf("First false index is %d\n", bitmap_scan(free_map, 0,1,false));
  /* Write bitmap to file. */
  free_map_file = file_open (inode_open (FREE_MAP_SECTOR));
  //printf("333\n");
  //inode_create : inode_disk를 만드는거고
  //inode_open : inode를 만들면서, inode -> data = inode_disk(fs_device에서 가져오는거)
  // open_inode list의 front에 넣는거네
/*struct inode *inode = free_map_file->inode;
printf("inode information : sector %d open_cnt %d removed %d deny_write_cnt %d\n",
            inode->sector,inode->open_cnt,inode->removed,inode->deny_write_cnt);
  struct inode_disk data = inode->data;
  printf("inode_disk infor : start %d length %d must be null\n",data.direct_blocks[0], data.length,data.direct_blocks[1]);

*/


  if (free_map_file == NULL)
    PANIC ("can't open free map");
//printf("before bitmap_write (freemap,freemapfile) at free_map_create, First false index is %d\n", bitmap_scan(free_map, 0,1,false));
//printf("cache table size %d\n",list_size(&cache_table));
  if (!bitmap_write (free_map, free_map_file))
    PANIC ("can't write free map");
//printf("after bitmap_write (freemap,freemapfile) at free_map_create, First false index is %d\n", bitmap_scan(free_map, 0,1,false));
//printf("First false index is %d\n", bitmap_scan(free_map, 0,1,false));
//printf("cache table size %d\n",list_size(&cache_table));
}