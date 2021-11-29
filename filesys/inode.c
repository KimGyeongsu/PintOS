#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/cache.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44


/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size) //0 :0, 1~512 : 1, 513~1024 : 2 ~~~~~
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

////////////////////////////////EXTENSIBLE////////////////////////////////////
static inline enum status_inode bytes_to_status_create (off_t size)
{
  if(bytes_to_sectors(size)<=121) return DIRECT;
  if(bytes_to_sectors(size)<=(121+128)) return INDIRECT;
  if(bytes_to_sectors(size)<=(121+128+128*128)) return D_DIRECT;
  PANIC("TOO MANY BLOCKS TO ALLOCATED\n");
}

static inline size_t bytes_to_offset_create (enum status_inode status, off_t size)
{
  if(status == DIRECT) return bytes_to_sectors(size); //0 : 0 1~512 : 1// 513 ~1024 : 2
  if(status == INDIRECT) return (bytes_to_sectors(size)-121);
  return (bytes_to_sectors(size)-121-128);
}

static inline enum status_inode bytes_to_status_read (off_t size) //0~511 : 1, 512~1023 : 2
{//size : 0 이고 length가 0이면?
  if(size/512+1<=121) return DIRECT;
  if(size/512+1<=(121+128)) return INDIRECT;
  if(size/512+1<=(121+128+128*128)) return D_DIRECT;
  PANIC("TOO MANY BLOCKS TO ALLOCATED\n");
}

static inline size_t bytes_to_offset_read (enum status_inode status, off_t size)
{
  if(status == DIRECT) return (size/512+1); //0~511 : 1, 512~1023 : 2
  if(status == INDIRECT) return (size/512+1-121);
  return (size/512+1-121-128);
}
/////////////////////////////////////////////////////////////////////////

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
/*
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);
  if (pos < inode->data.length)
    return inode->data.start + pos / BLOCK_SECTOR_SIZE;
  else
    return -1;
}
*/
/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  lock_init(&fs_lock);
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */

bool
inode_create (block_sector_t sector, off_t length, block_sector_t prev_sector, bool is_dir)
{
  struct inode_disk *disk_inode = NULL;
  ASSERT (length >= 0);
  //printf("bitmap_scan_and_flip_result %d\n", bitmap_scan_and_flip (free_map, 0, 0, false));
  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  //A,B변수로받아서 A*B만큼의 크기를 할당
  if (disk_inode != NULL)
    {
      size_t sectors = bytes_to_sectors (length);
      ////////////////////EXTENSIBLE/////////////////////////////
      enum status_inode status = bytes_to_status_create(length);
      block_sector_t block_offset = bytes_to_offset_create(status, length);
      //p//rintf("block off %d\n",block_offset);
      ////////////////////EXTENSIBLE////////////////////////////
      //실제로 필요한 block의 개수
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      disk_inode->prev_sector = prev_sector;
      disk_inode ->is_dir = is_dir;
      disk_inode ->pos = 0;
      //printf("file_map %x\n",free_map_file);
      if(free_map_file==NULL){
        if(free_map_allocate_extensible(status, block_offset, disk_inode,false))
        {
          lock_acquire(&fs_lock);
          block_write (fs_device, sector, disk_inode);
          lock_release(&fs_lock);
          free (disk_inode);
          return true; 
        }
      }
      else {
        if(free_map_allocate_extensible(status, block_offset, disk_inode,true))
        {
          lock_acquire(&fs_lock);
          block_write (fs_device, sector, disk_inode);
          lock_release(&fs_lock);
          free (disk_inode);
          return true;
        }
      }
    }
  return false;
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
  inode->prev_sector = inode->data.prev_sector;
  inode->is_dir = inode->data.is_dir;
  lock_init(&inode->extend_lock);
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

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  //printf("CLOSE ENTER : free_map : first false index is %d\n", bitmap_scan(free_map, 0,1,false));
  /* Ignore null pointer. */
  //printf("close\n");
  if (inode == NULL)
    return;
  //printf("inode->sector %d\n", inode->sector);

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      ///////////////////////////////////////////////////////////////////////
      /*get_rid_from cache table*/
     // printf("At inode close : cache table size %d\n",list_size(&cache_table));

  //printf("inode information : sector %d open_cnt %d removed %d deny_write_cnt %d\n",
    //        inode->sector,inode->open_cnt,inode->removed,inode->deny_write_cnt);
  //printf("inode_disk infor : start %d length %d\n",data.start, data.length);
      remove_and_flush_cte(inode);
    //  printf("cache table size %d\n",list_size(&cache_table));
      ///////////////////////////////////////////////////////////////////////
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          free_map_release (inode->sector, 1);
          //free_map_release (inode->data.start, bytes_to_sectors (inode->data.length));
          enum status_inode status = bytes_to_status_create(inode->data.length);
          block_sector_t block_offset = bytes_to_offset_create(status, inode->data.length);
          //printf("status %d, block_offset %d\n",status,block_offset);
          free_map_release_extensible(status, block_offset, &inode->data); 
        }

      free (inode); 
    }
     //printf("free_map : first false index is %d\n", bitmap_scan(free_map, 0,1,false));
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
///////////////////////EXTENSIBLE////////////////////////////////
block_sector_t find_sector_index (const struct inode *inode, off_t pos){

  ASSERT (inode != NULL);
  if(pos >= inode->data.length)
    return -1;

  struct inode_disk *inode_disk = &inode->data;
   // printf("pos %d\n",pos); pos 0
  enum status_inode status = bytes_to_status_read(pos);
  block_sector_t block_offset = bytes_to_offset_read(status, pos);
    //printf("block_offset %d\n",block_offset);
  block_sector_t result;
//////////DIERCT/////////////////////////////
  if(status==DIRECT) return inode_disk->direct_blocks[block_offset-1];
////////////////////////////////////////////////
////////////INDIRECT///////////////////////////////////
  else if(status==INDIRECT) {
    struct indirect_block *indirect_block = malloc(sizeof(struct indirect_block));
    block_read (fs_device, inode_disk->indirect_block_index, indirect_block);
    result = indirect_block->block_index[block_offset-1];
    free(indirect_block);
    return result;
  }
////////////////////////////////////////////////////////////
  //////////////D_DIRECT//////////////////////////////////////
  else if(status==D_DIRECT) {
    int num_of_indirect_block = block_offset / 128;
    int remainder = block_offset % 128;

    struct d_indirect_block *d_indirect_block = malloc(sizeof(struct d_indirect_block));
    block_read (fs_device, inode_disk->d_indirect_block_index, d_indirect_block);

    struct indirect_block *indirect_block = malloc(sizeof(struct indirect_block));
    if(remainder==0) {
      block_read (fs_device, d_indirect_block->indirect_block_index[num_of_indirect_block-1], indirect_block); 
      result = indirect_block->block_index[127]; 
    }      
    else {
      block_read (fs_device, d_indirect_block->indirect_block_index[num_of_indirect_block], indirect_block);
      result = indirect_block->block_index[remainder-1];
    }
    free(indirect_block);
    free(d_indirect_block);
    return result;
  }
///////////////////////////////////////////////////////////////////
  else PANIC("STATUS WRONG\n");
}
////////////////////////////////////////////////////////////////


off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  //printf("INODE_READ_AT_START\n");
  //printf("READ ENTER : free_map : first false index is %d\n", bitmap_scan(free_map, 0,1,false));
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  //uint8_t *bounce = NULL; 
  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      //block_sector_t sector_idx = byte_to_sector (inode, offset); //실제 file disk의 index
///////////////////////EXTENSIBLE////////////////////////////////
      block_sector_t sector_idx = find_sector_index(inode, offset);
/////////////////////////////////////////////////
     //if(sector_idx < block_size(fs_device)){
       // read_ahead(sector_idx+1);
      //}
      int sector_ofs = offset % BLOCK_SECTOR_SIZE; //그 sector(disk의 index)안에서의 실제 offset
      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset; // total filesize - offset
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs; // BLOCK_SECTOR_SIZE - sector_offset
      int min_left = inode_left < sector_left ? inode_left : sector_left;
      //min_left : 진짜로 이게 진짜 실제로 읽어야 할 거(이 sector에서)
      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      //얘가 진짜 읽어야 하는거네 ㅋㅋㅋㅋ 
      if (chunk_size <= 0)
        break;
//////////////////////////////////////////////////////////////////////////////////////////
      struct cte *cte;
      
///////////////////////////////READ_AHEAD/////////////////////////////////
      block_sector_t next_sector_idx = find_sector_index(inode, offset+chunk_size);
      if(next_sector_idx != -1)
      {
        cte = get_cte(next_sector_idx);
        if(cte==NULL)
        {
          if(list_size(&cache_table)!=64)
          {
            //printf("bbb\n");
            if((cte=create_cte(next_sector_idx,inode))==NULL) PANIC("creation cte : fail");
              //printf("ccc\n");
          }
        else
        {
      //printf("ddd\n");
          cte = find_victim_cte();
          cte->evictable = false;
          flush_cte(cte);
          update_cte(cte,next_sector_idx,inode);
          //printf("333\n");
        }
        //printf("eee\n");
        cte->evictable = true;
          }
      }
  //////////////////////////////////////////////////
      cte = get_cte(sector_idx);
      //lock_release(&cache_lock);

      if(cte==NULL){
        //At the cache table, cte exists
        if(list_size(&cache_table)!=64){
          //cache table is not full!
          //printf("READ : no cte exist : cache size %d\n",list_size(&cache_table));
          if((cte=create_cte(sector_idx, inode))==NULL) PANIC("CREATION CTE : FAIL");
        }
        else{
          cte=find_victim_cte();
          //lock_acquire(&cte->cte_lock);
          cte->evictable = false;
          flush_cte(cte);
          update_cte(cte,sector_idx,inode);
          //lock_release(&cte->cte_lock);
        }
      }
      else {//cache table hit
        //lock_acquire(&cte->cte_lock);
        cte->evictable = false;
      }
      //lock_acquire(&cte->cte_lock);
      uint8_t *a = cte->data;
      /*
      if(lock_held_by_current_thread(&inode->extend_lock)){
        lock_acquire(&inode->extend_lock);
        memcpy(buffer + bytes_read, a+sector_ofs, chunk_size);
        lock_release(&inode->extend_lock);
      }
      else{*/
        memcpy(buffer + bytes_read, a+sector_ofs, chunk_size);
      //}
      //lock_release(&cte->cte_lock);
      
      cte->evictable = true;
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  //free (bounce);
    ///printf("INODE_READ_AT_DONE\n");
  //printf("free_map : first false index is %d\n", bitmap_scan(free_map, 0,1,false));
  //printf("bytes_Read : %d\n",bytes_read);
  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  //printf("INODE_WRITE_AT_START\n");
  //printf("WRITE ENTER : free_map : first false index is %d\n", bitmap_scan(free_map, 0,1,false));
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
 // uint8_t *bounce = NULL;
 int extend_flag = 0;

  if (inode->deny_write_cnt)
    return 0;

  lock_acquire(&inode->extend_lock); //added

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      //block_sector_t sector_idx = byte_to_sector (inode, offset);
      block_sector_t sector_idx = find_sector_index(inode, offset);//////////EXTENSIBLE
      
        int sector_ofs = offset % BLOCK_SECTOR_SIZE;
        /* Bytes left in inode, bytes left in sector, lesser of the two. */
        off_t inode_left = inode_length (inode) - offset;
        int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
        int min_left = inode_left < sector_left ? inode_left : sector_left;
        /* Number of bytes to actually write into this sector. */
        int chunk_size = size < min_left ? size : min_left;
        //실제로 쓰는 크기
      if(sector_idx!=-1)
      {
        if(extend_flag==0){//첫 while loop가 여기로!
          lock_release(&inode->extend_lock); //extend 된 적 없으면 extend lock release;
          extend_flag = 1;
        }
        if (chunk_size <= 0)
          break;
  //////////////////////////////////////////////////
        struct cte *cte;
        cte = get_cte(sector_idx);
        
        if(cte==NULL){
          //At the cache table, cte exists
          if(list_size(&cache_table)!=64){
            //cache table is not full!
            cte=create_cte(sector_idx,inode);
            if(cte==NULL) PANIC("CREATION CTE : FAIL");
            //lock_acquire(&cte->cte_lock); //added
          }
          else{
            //lock_acquire(&cte->cte_lock); //added
            cte=find_victim_cte();
            cte->evictable = false;
            flush_cte(cte);
            update_cte(cte,sector_idx,inode);
          }
        }
        else {//cache table hit
          //lock_acquire(&cte->cte_lock); //added
          cte->evictable = false;
        }
        //printf("lock acquired ");
        lock_acquire(&cte->cte_lock); //added
        //printf("& done\n");
        uint8_t *a = cte->data;
        memcpy(a + sector_ofs, buffer+bytes_written, chunk_size);
        cte->dirty = true;
        cte->evictable = true;
        //printf("lock release");
        lock_release(&cte->cte_lock); //added
        //printf(" & done\n");
      }
      else
      {
        if(bitmap_scan(free_map,0,1,false)==-1){
          lock_release(&inode->extend_lock);
          return bytes_written;
        }
        if(extend_flag ==1){ //extend하려고 처음 들어오는 순간 lock 걸고 flag true로 설정
          //printf("lock acquired here");
          lock_acquire(&inode->extend_lock);
          //printf(" and done\n");
        }
        extend_flag = 2;
        //이 시점부터 write 끝날때까지 묶여있는기다
        struct inode_disk *inode_disk = &inode -> data; // inode_disk
        /* inode_disk_information update */
        enum status_inode past_status = bytes_to_status_create(inode_disk->length); //past_status
        block_sector_t past_offset = bytes_to_offset_create(past_status, inode_disk->length);//past_offset

        int length = 0;
        if(offset < inode_disk->length){
          PANIC("offset is smaller than length\n");
        }
        else if(offset == inode_disk->length){
          length = inode_disk -> length + size;
        }
        else{
          length = offset+size;
        }
        /////////////inode_disk -> length += size;
        enum status_inode cur_status = bytes_to_status_create(length); //cur_status
        block_sector_t cur_offset = bytes_to_offset_create(cur_status, length);//cur_offset

        /*update inode disk -> free_map_allocate_extensible랑 매우 유사한 함수를 하나 만들자 */
        // (1) free_map, free_map_file 반영
        // (2) 0으로 할당
        cnt00000 = 0;
        update_inode_disk_write(inode_disk, past_status, past_offset, cur_status, cur_offset);
        //if(offset == inode_disk->length){
        if(bitmap_scan(free_map,0,1,false) != -1)
        {
          inode_disk -> length = length;
        }
        else
        {
          inode_disk ->length = DIV_ROUND_UP(inode_disk->length + cnt00000*512, 512);
        }
        //}
        //write inode information to disk
        //lock_acquire(&fs_lock);
        block_write(fs_device, inode->sector, inode_disk);
        //lock_release(&fs_lock);
        //lock_release(&inode->extend_lock);
        continue;
      }
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
 //free (bounce);
   // printf("INODE_WRITE_AT_DONE\n");
     //printf("free_map : first false index is %d\n", bitmap_scan(free_map, 0,1,false));
    //printf("bytes_write : %d\n",bytes_written);
  if(extend_flag == 2){
    //printf("lock released outside ");
    lock_release(&inode->extend_lock);
    //printf(" and done ");
  }
  //lock_release(&inode->extend_lock);
  
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
/////////////////////////////////////////////////////////////////////////////////////////////////////
/*update inode disk -> free_map_allocate_extensible랑 매우 유사한 함수를 하나 만들자 */
        // (1) free_map, free_map_file 반영
        // (2) 0으로 할당
static void  extend_direct_direct(size_t start, size_t end, struct inode_disk * inode_disk, bool flag);
static void  extend_direct_indirect(size_t end, struct inode_disk * inode_disk, bool flag);
static void  extend_indirect_indirect(size_t start, size_t end, struct inode_disk * inode_disk, bool flag);
static void  extend_indirect_dindirect(size_t end, struct inode_disk * inode_disk);
static void  extend_dindirect_dindirect(size_t start, size_t end, struct inode_disk * inode_disk, bool flag);

void update_inode_disk_write(struct inode_disk *inode_disk, enum status_inode past_status, 
        block_sector_t past_offset, enum status_inode cur_status, block_sector_t cur_offset)
{
  lock_acquire(&fs_lock);
  if(past_status==DIRECT && cur_status==DIRECT)
  {
    if(past_offset>cur_offset) PANIC("ERROR\n");
    extend_direct_direct(past_offset, cur_offset, inode_disk, true);
    lock_release(&fs_lock);
    return;
  }
  else if(past_status==DIRECT && cur_status==INDIRECT)
  {
    extend_direct_direct(past_offset, 121, inode_disk, false);
    extend_direct_indirect(cur_offset, inode_disk, true);
    lock_release(&fs_lock);
    return;
  }
  else if(past_status==DIRECT && cur_status==D_DIRECT)
  {
    extend_direct_direct(past_offset, 121, inode_disk, false);
    extend_direct_indirect(128, inode_disk, false); //indirect block이 생성이 된적이 없는데 되었따고 가정하고 썼네 이거 고치자!
    extend_indirect_dindirect(cur_offset, inode_disk);
    lock_release(&fs_lock);
    return;

  }
  else if(past_status==INDIRECT & cur_status==INDIRECT)
  {
    if(past_offset>cur_offset) PANIC("ERROR\n");
    extend_indirect_indirect(past_offset, cur_offset, inode_disk, true);
    lock_release(&fs_lock);
    return;
  }
  else if(past_status==INDIRECT & cur_status==D_DIRECT)
  {
    extend_indirect_indirect(past_offset, 128, inode_disk, false);
    extend_indirect_dindirect(cur_offset, inode_disk);
    lock_release(&fs_lock);
  }
  else if(past_status==D_DIRECT & cur_status==D_DIRECT)
  {
    if(past_offset>cur_offset) PANIC("ERROR\n");
    extend_dindirect_dindirect(past_offset, cur_offset, inode_disk, true);
    lock_release(&fs_lock);
    return;
  }
  else PANIC("ERROR\n");
}

static void extend_direct_direct(size_t start, size_t end, struct inode_disk * inode_disk, bool flag)
{ //start번째 블럭은 [start-1] 에 기록되어 있고, end번째 블럭은 [end-1]에 기록되어야하함.
  //즉 [start]부터 [end-1]까지의 기록이 목적
  if(start==end) return;
  for(int i=start; i<end; i++)
  {
    block_sector_t sector = bitmap_scan_and_flip(free_map,0,1,false);
    if(sector == -1) goto done;
    cnt00000++;
    inode_disk->direct_blocks[i] = sector;

    static char zeros[BLOCK_SECTOR_SIZE];
    //lock_acquire(&fs_lock);
    block_write (fs_device, sector, zeros);
    //lock_release(&fs_lock);
  }
  done:
  if(flag) bitmap_write(free_map, free_map_file);
}

static void extend_direct_indirect(size_t end, struct inode_disk * inode_disk, bool flag)
{
  block_sector_t indirect_block_index = bitmap_scan_and_flip(free_map,0,1,false);
  if(indirect_block_index == -1) goto done;
  inode_disk->indirect_block_index = indirect_block_index;

  struct indirect_block *indirect_block = malloc(sizeof(struct indirect_block));
  for(int i=0; i<end; i++)
  {
    block_sector_t sector = bitmap_scan_and_flip(free_map,0,1,false);
    if(sector == -1)
    {
      free(indirect_block);
      goto done;
    }
    cnt00000++;
    indirect_block->block_index[i] = sector;

    static char zeros[BLOCK_SECTOR_SIZE];
    //lock_acquire(&fs_lock);
    block_write (fs_device, sector, zeros);
    //lock_acquire(&fs_lock);
  }

  block_write(fs_device, indirect_block_index, indirect_block);
  free(indirect_block);

  done:
  if(flag) bitmap_write(free_map, free_map_file);
}

static void extend_indirect_indirect(size_t start, size_t end, struct inode_disk * inode_disk, bool flag)
{ 
  if(start==end) return;

  struct indirect_block *indirect_block = malloc(sizeof(struct indirect_block));
  block_read(fs_device, inode_disk->indirect_block_index, indirect_block);
  for(int i=start; i<end; i++)
  {
    block_sector_t sector = bitmap_scan_and_flip(free_map,0,1,false);
    if(sector ==-1)
    {
      free(indirect_block);
      goto done;
    }
    cnt00000++;
    indirect_block->block_index[i] = sector;

    static char zeros[BLOCK_SECTOR_SIZE];
    block_write (fs_device, sector, zeros);
  }
  block_write(fs_device, inode_disk->indirect_block_index, indirect_block);
  free(indirect_block);

  done:
  if(flag) bitmap_write(free_map, free_map_file);
}

static void extend_indirect_dindirect(size_t end, struct inode_disk * inode_disk)
{
  int num_of_indirect_block = end / 128;
  int remainder = end % 128;
  /*d_indirect allocation*/
  block_sector_t dindirect_block_index = bitmap_scan_and_flip(free_map,0,1,false);
  if(dindirect_block_index==-1) goto done;
  inode_disk->d_indirect_block_index = dindirect_block_index;

  /*d_indirect_block에 실제 메모리 할당*/
  struct d_indirect_block * d_indirect_block = malloc(sizeof(struct d_indirect_block));

  //d_indirect_block의 indirect_block_index[i]에 대해서 free_map_위에 함수 실행
  for(int i=0; i<num_of_indirect_block; i++)
  {
    block_sector_t indirect_block_index = bitmap_scan_and_flip(free_map,0,1,false);
    if(indirect_block_index==-1)
    {
      free(d_indirect_block);
      goto done;
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
        free(d_indirect_block);
        free(indirect_block);
        goto done;
      }
      cnt00000++;
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
      free(d_indirect_block);
      goto done;
    }

    d_indirect_block->indirect_block_index[num_of_indirect_block] = indirect_block_index;
  /*bounce buffer와 비슷하게 역할을 하는 indirect_block을 malloc해옴. 
    이에 기록함.(indirect_block->block_index에 sector를 저장)*/
    struct indirect_block *indirect_block = malloc(sizeof(struct indirect_block));
    for(int i=0; i<remainder; i++)
    {
      block_sector_t sector = bitmap_scan_and_flip(free_map,0,1,false);
      if(sector == -1)
      {
        free(d_indirect_block);
        free(indirect_block);
        goto done;
      }
      cnt00000++;
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
  free(d_indirect_block);

  done:
  bitmap_write(free_map, free_map_file);
}

static void extend_dindirect_dindirect(size_t start, size_t end, struct inode_disk * inode_disk, bool flag)
{ // start !=0
  if(start==end) return;


  struct d_indirect_block *d_indirect_block = malloc(sizeof(struct d_indirect_block));
  block_read(fs_device, inode_disk->d_indirect_block_index, d_indirect_block);

  int num_start = start / 128;
  int remainder_start = start % 128;
  int num_end = end / 128;
  int remainder_end = end % 128;

  if(num_start == num_end) //몫이 같은 경우 128~255의 경우를 생각해봅시다.
  {
    if(remainder_start ==0) //new block 할당   //128과 255 같은 케이스 다뤄주기 위함.
    {
      block_sector_t indirect_block_index = bitmap_scan_and_flip(free_map,0,1,false);
      if(indirect_block_index==-1)
      {
        free(d_indirect_block);
        goto done1;
      }
      d_indirect_block->indirect_block_index[num_start] = indirect_block_index;
      /*bounce buffer와 비슷하게 역할을 하는 indirect_block을 malloc해옴. 
      이에 기록함.(indirect_block->block_index에 sector를 저장)*/
      struct indirect_block *indirect_block = malloc(sizeof(struct indirect_block));
      for(int i=0; i<remainder_end; i++)
      {
        block_sector_t sector = bitmap_scan_and_flip(free_map,0,1,false);
        if(sector==-1)
        {
          free(indirect_block);
          free(d_indirect_block);
          goto done1;
        }
        cnt00000++;
        indirect_block->block_index[i] = sector;

        static char zeros[BLOCK_SECTOR_SIZE];
        block_write (fs_device, sector, zeros);
      }
      /*free하기 전, 이 indirect_block을 inode_disk->indirect_block_index에 적어야함*/
      block_write(fs_device, indirect_block_index, indirect_block);
      /*free indirect_block*/
      free(indirect_block);
    }
    else  //129와 255같은 케이스
    {
      /*bounce buffer와 비슷하게 역할을 하는 indirect_block을 malloc해옴. 
      이에 기록함.(indirect_block->block_index에 sector를 저장)*/
      struct indirect_block *indirect_block = malloc(sizeof(struct indirect_block));
      block_read(fs_device, d_indirect_block->indirect_block_index[num_start], indirect_block);
      for(int i=remainder_start; i<remainder_end; i++)
      {
        block_sector_t sector = bitmap_scan_and_flip(free_map,0,1,false);
        if(sector ==-1)
        {
          free(indirect_block);
          free(d_indirect_block);
          goto done1;
        }
        cnt00000++;
        indirect_block->block_index[i] = sector;

        static char zeros[BLOCK_SECTOR_SIZE];
        block_write (fs_device, sector, zeros);
      }
      /*free하기 전, 이 indirect_block을 inode_disk->indirect_block_index에 적어야함*/
      block_write(fs_device, d_indirect_block->indirect_block_index[num_start], indirect_block);
      /*free indirect_block*/
      free(indirect_block);
    }
    block_write(fs_device, inode_disk->d_indirect_block_index, d_indirect_block);
    free(d_indirect_block);
    if(flag) bitmap_write(free_map, free_map_file);
    done1:
    return;
  }

  else  //num_start < num_end : 128~255(몫이 1) 사이의 숫자와 384~511(몫이 3)사이의 숫자의 경우를 생각해봅시다.
  {
    for(int i=num_start; i<num_end; i++)
    {
      if(i==num_start && remainder_start!=0) //129(1,1)와 384(3,0)의 경우
      {
        struct indirect_block *indirect_block = malloc(sizeof(struct indirect_block));
        block_read(fs_device, d_indirect_block->indirect_block_index[num_start], indirect_block);
        for(int j=remainder_start; j<128; j++)
        {
          block_sector_t sector = bitmap_scan_and_flip(free_map,0,1,false);
          if(sector == -1)
          {
            free(indirect_block);
            free(d_indirect_block);
            goto done2;
          }
          indirect_block->block_index[j] = sector;
          cnt00000++;
          static char zeros[BLOCK_SECTOR_SIZE];
          block_write (fs_device, sector, zeros);
        }
        block_write(fs_device, d_indirect_block->indirect_block_index[num_start], indirect_block);
        free(indirect_block);
      }
      else // 128(1,0)과 385(3,1)의 경우
      {
        block_sector_t indirect_block_index = bitmap_scan_and_flip(free_map,0,1,false);
        if(indirect_block_index==-1)
        {
          free(d_indirect_block);
          goto done2;
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
            free(d_indirect_block);
            free(indirect_block);
            goto done2;
          }
          indirect_block->block_index[j] = sector;
          cnt00000++;
          static char zeros[BLOCK_SECTOR_SIZE];
          block_write (fs_device, sector, zeros);
        }
        block_write(fs_device, indirect_block_index, indirect_block);
        free(indirect_block);
      } 
    }
  }

  if(remainder_end!=0) ////129(1,1)와 385(3,1)의 경우
  {
    block_sector_t indirect_block_index = bitmap_scan_and_flip(free_map,0,1,false);
    if(indirect_block_index==-1)
    {
      free(d_indirect_block);
      goto done2;
    }
    d_indirect_block->indirect_block_index[num_end] = indirect_block_index;

    struct indirect_block *indirect_block = malloc(sizeof(struct indirect_block));
    for(int i=0; i<remainder_end; i++)
    {
      block_sector_t sector = bitmap_scan_and_flip(free_map,0,1,false);
      if(sector == -1)
      {
        free(d_indirect_block);
        free(indirect_block);
        goto done2;
      }
      indirect_block->block_index[i] = sector;
      cnt00000++;
      static char zeros[BLOCK_SECTOR_SIZE];
      block_write (fs_device, sector, zeros);
    }
    /*free하기 전, 이 indirect_block을 inode_disk->indirect_block_index에 적어야함*/
    block_write(fs_device, indirect_block_index, indirect_block);
    /*free indirect_block*/
    free(indirect_block);
  }

  block_write(fs_device, inode_disk->d_indirect_block_index, d_indirect_block);
  free(d_indirect_block);

  done2:
  if(flag) bitmap_write(free_map, free_map_file);
}