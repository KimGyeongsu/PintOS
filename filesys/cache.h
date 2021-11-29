#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include <list.h>
#include "threads/synch.h"
#include "devices/block.h"

struct list cache_table; //64개 버퍼 캐시 테이블
struct lock cache_lock; // UNUSED NOW, only initialized

struct cte {
   struct inode *inode; //to use in file_close(inode_close)
 

   struct list_elem elem; //list element
   bool dirty; //dirty인지 -> if dirty -> evict 시 file write

   //file contents
   block_sector_t index; //block device에서의 위치. 무조건 필요함. 바로 적을 수 있잖아
   void *data; //physical memory로 받아온 실제 data의 위치

   //synchronization
   struct lock cte_lock; //UNUSED NOW, only initialized
   //evictable
   bool evictable;
};

struct cte* get_cte(block_sector_t index);
//cte table에서 cte찾음. 못찾으면 null return
struct cte* create_cte(block_sector_t index, struct inode *inode);
//cte create. 실패시 null return (만드는 순간 evictable false)
struct cte* find_victim_cte(void);
//FIFO로 find_victim
void flush_cte(struct cte* cte);
//cte가 dirty하면 내용 더해줌. 
//성공하면 dirty false로 바꿔줌.
void update_cte(struct cte* cte, block_sector_t index, struct inode *inode);
//index 저장. 그리고 data에 내용 바꿔줌
void flush_and_free_cache_table(void);
//filesys_done

void remove_and_flush_cte(struct inode *inode);
//file_close 시 cte entry 없애줌
/////////////////////////////////////////////////////////////////////////////
void periodically_flush_all(void *aux);
//write back asynchronization : helper function of below
void write_back_peoridically(void);
//make thread that write_back
void helper_read_ahead(void *aux);
//read_ahead helepr func
void read_ahead(block_sector_t index);
//read_ahead
/////////////////////EXTENSIBLE///////////////////////////
struct cte* get_cte_inode(struct inode *inode);





#endif /* filesys/cache.h */
