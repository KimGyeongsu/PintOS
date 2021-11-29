#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include <list.h>
#include "filesys/off_t.h"
#include "devices/block.h"
#include "threads/synch.h"

int cnt00000;

/*/////////////////////////////
Extensible file

1) File System Partition : 8MB : 16384 block
support 하도록 구현해야 함 (한 file : 하나의 indoe_disk : 512B 안넘게) + 외부단편화 안일어나게

2) file Growth 구현 : EOF에서 write하면 extend 되어야 함.
3) Root directory file들에도 똑같이 적용. 16 files limit 풀어줘야 함.

4) seek beyond EOF 가능. 그 gap은 0으로 채우자. 그리고 past EOF에서의 reading은 no bytes return

5) 4)에서 0으로 채워진 these blocks 들은 explicity written 될 때 까지 allocate 하지 않음.
*/

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
//struct inode_disk
  //{
    //block_sector_t start; //4               /* First data sector. */
    //off_t length;  //4                     /* File size in bytes. */
    //unsigned magic;  //4           /* Magic number. */
    //uint32_t unused[125]; //500              /* Not used. */
  //};
///////////////////////////////////////EXTENSIBLE/////////////////////////////////////////////
struct inode_disk
{
	off_t length;
	unsigned magic;
	block_sector_t direct_blocks[121];
	block_sector_t indirect_block_index; 
	//Status == D-direct이고 몫이 1, 나머지가 3이라카면 실제로는
	block_sector_t d_indirect_block_index; //얘는 1이고
	//d_inderect_block ->indirect_block_index[2]까지 내용이 적혀있음
  block_sector_t prev_sector;
  int pos;
    //prev_sector : 0 : root directory_inode_disk (sector : 1)
  //prev_sector : -1 : file_map_inode_disk (sector : 0)
  // 나머지는 속해 있는 directory의 inode_disk의 sector를 나타냄
  bool is_dir; //directory or not
  char unused[3];
};

enum status_inode {
    DIRECT, INDIRECT, D_DIRECT
};

struct indirect_block
{
	block_sector_t block_index[128];
};

struct d_indirect_block{
  block_sector_t indirect_block_index[128];
};
///////////////////////////////////////////////////////////////////////////////////////////////

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */
    bool is_dir; //this inodes is directory or not
    block_sector_t prev_sector;
    struct lock extend_lock;
    //prev_sector : 0 : root directory_inode_disk (sector : 1)
    //prev_sector : -1 : file_map_inode_disk (sector : 0)
    // 나머지는 속해 있는 directory의 inode_disk의 sector를 나타냄
  };

struct lock fs_lock;

struct bitmap;

void inode_init (void);
bool inode_create (block_sector_t, off_t, block_sector_t, bool);
struct inode *inode_open (block_sector_t);
struct inode *inode_reopen (struct inode *);
block_sector_t inode_get_inumber (const struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (const struct inode *);

/////////EXTENSIBLE///////////
block_sector_t find_sector_index
(const struct inode *inode, off_t pos);

void update_inode_disk_write(struct inode_disk *inode_disk, enum status_inode past_status, 
   block_sector_t past_offset, enum status_inode cur_status, block_sector_t cur_offset);
///////////////////////////////////////////////////////
#endif /* filesys/inode.h */
