#include "filesys/cache.h"
#include "filesys/filesys.h"
#include "devices/block.h"
#include "threads/synch.h"
#include <debug.h>

#define SLEEP_TIME 500


struct cte* get_cte(block_sector_t index){
  struct cte *cte =NULL;
  struct list_elem *e;
  //printf("get_cte : list_size is %d\n", list_size(&cache_table));
  if(list_empty(&cache_table)) return cte; //if cache table size : 0 -> return NULL
  for (e = list_begin(&cache_table); e != list_end(&cache_table); e = list_next(e)) {
   // printf("index is %d\n",list_entry(e, struct cte, elem)->index);
    if (list_entry(e, struct cte, elem)->index == index) {
      //printf("real index\n");
      cte = list_entry(e, struct cte, elem);
      break;
    }
  }
  return cte;
}

struct cte* create_cte(block_sector_t index, struct inode *inode){
    struct cte *cte=malloc(sizeof(struct cte));
    if(cte==NULL) return NULL;

    cte->inode =inode;
    cte->dirty = false;
    cte->index = index;

    cte->data = malloc(BLOCK_SECTOR_SIZE);
    if(cte->data==NULL) return NULL;
    block_read (fs_device, index, (uint8_t*)cte->data);

    lock_init(&cte->cte_lock);
    cte->evictable=false;

    list_push_back(&cache_table, &cte->elem);
    return cte;
}

struct cte* find_victim_cte (void) {
  struct list_elem *evict_elem;
  struct cte *victim;

  while(1)
  {
    evict_elem = list_pop_front(&cache_table);
    victim = list_entry (evict_elem, struct cte, elem);   
    list_push_back(&cache_table, evict_elem); 
    if(victim->evictable==true) break;
  }
  
  return list_entry (evict_elem, struct cte, elem);
}

void flush_cte(struct cte* cte){
  if(cte->dirty == false) return;
  lock_acquire(&cte->cte_lock);
  block_write(fs_device, cte->index, (uint8_t*)cte->data);
  cte->dirty = false;
  lock_release(&cte->cte_lock);
}

void update_cte(struct cte* cte, block_sector_t index, struct inode *inode){
  cte->index = index;
  cte->inode = inode;
  block_read (fs_device, index, (uint8_t*)cte->data);
}

void flush_and_free_cache_table(void){
  struct list_elem *e;
  struct list_elem *f;
  struct cte *cte;
  for (e = list_begin(&cache_table); e != list_end(&cache_table); e = f){
            cte = list_entry(e, struct cte, elem);
            f=list_next(e);
            list_remove(e);
            //lock_acquire(&cte->cte_lock); //added
            flush_cte(cte);
            //lock_release(&cte->cte_lock); //added
            free(cte->data);
            free(cte);
        }
}

void remove_and_flush_cte(struct inode *inode){
  struct list_elem *e;
  struct list_elem *f;
  struct cte *cte;
  for (e = list_begin(&cache_table); e != list_end(&cache_table); e = f){
    cte = list_entry(e, struct cte, elem);
    f=list_next(e);
    if(cte->inode==inode){
      list_remove(e);
      //lock_acquire(&cte->cte_lock); //added
      flush_cte(cte);
      //lock_release(&cte->cte_lock); //added
      free(cte->data);
      free(cte);
    }
  }

}

void periodically_flush_all(void *aux){
    struct list_elem *e;
    struct cte *cte;
    while(1){
        timer_sleep(SLEEP_TIME);

        for (e = list_begin(&cache_table); e != list_end(&cache_table); e = list_next(e)){
            cte = list_entry(e, struct cte, elem);
            //lock_acquire(&cte->cte_lock); //added
            cte->evictable = false;
            flush_cte(cte);
            cte->evictable = true;
            //lock_release(&cte->cte_lock); //added
        }

    }
}

void write_back_peoridically(void){
  thread_create("write_back", 31, periodically_flush_all,NULL);
}
/*
void helper_read_ahead(void *aux)
{
    block_sector_t index = *(block_sector_t *)aux;
////////////////////////////////////////////////////////////////////////////
    struct cte *cte;
    cte = get_cte(index);
    if(cte==NULL){
      if(list_size(&cache_table)!=64){
        if((cte=create_cte(index, inode))==NULL) PANIC("CREATION CTE : FAIL");
      }
      else{
        cte=find_victim_cte();
        cte->evictable = false; 
        flush_cte(cte);
        update_cte(cte,index,inode);
      }
      cte->evictable = true;
    }   
////////////////////////////////////////////////////////////////////////////
}

void read_ahead(block_sector_t index)
{
    block_sector_t idx = index+1;
    thread_create("read_ahead", 31, helper_read_ahead, &idx);
}
*/
struct cte* get_cte_inode(struct inode *inode){
  struct cte *cte =NULL;
  struct list_elem *e;
  //printf("get_cte : list_size is %d\n", list_size(&cache_table));
  if(list_empty(&cache_table)) return cte; //if cache table size : 0 -> return NULL
  for (e = list_begin(&cache_table); e != list_end(&cache_table); e = list_next(e)) {
   // printf("index is %d\n",list_entry(e, struct cte, elem)->index);
    if (list_entry(e, struct cte, elem)->inode == inode) {
      //printf("real index\n");
      cte = list_entry(e, struct cte, elem);
      break;
    }
  }
  return cte;
}