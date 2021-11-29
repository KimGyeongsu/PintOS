#include "lib/kernel/hash.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/syscall.h"
#include "vm/mmap.h"
#include "vm/page.h"
#include "vm/frame.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include <bitmap.h>

void init_mte_table(void){
  struct thread *cur = thread_current();
  list_init(&cur->mte_list);
  sema_init(&cur->mte_sema, 1);
}


int mmap(int fd, void *uaddr){
  struct file *open_file = get_file_from_fd(fd);
  int file_length0; //total file lenth
  int pg_num; //total number of pages to allocated
  int zero_bytes=0; //file length가 PGSIZE 배수 아닐경우 할당해야 할 zero bytes의 수
  struct thread *cur = thread_current();

  if(!open_file || !(file_length0=file_length(open_file)) || pg_ofs(uaddr) || 
          !uaddr || !fd || fd==1){
    return -1;
  }

  open_file = file_reopen(open_file);
  file_deny_write(open_file);

  pg_num = file_length0 / PGSIZE;
  if(file_length0 % PGSIZE){
    pg_num++;
    zero_bytes = PGSIZE - file_length0 % PGSIZE;
  }

  struct bitmap *writable_bitmap = bitmap_create(pg_num);
  if(!setting_bitmap(writable_bitmap, open_file,pg_num)){
    file_close(open_file);
    bitmap_destroy(writable_bitmap);
    return -1;
  }

  bool result = check_exist_empty_region_spt(cur->spt, uaddr, pg_num);
  if(!result) {
    file_close(open_file);
    bitmap_destroy(writable_bitmap);
    return -1;
  }

  struct mte *mte = initialize_mte(pg_num,open_file, zero_bytes, uaddr,writable_bitmap);
  if(!mte) {
    file_close(open_file);
    bitmap_destroy(writable_bitmap);
    return -1;
  }

  if(!creates_sptes_mmap(mte)){
    file_close(open_file);
    bitmap_destroy(writable_bitmap);
    return -1;
  }
  file_allow_write(open_file);
  return mte->mapid;
}

bool setting_bitmap(struct bitmap *bitmap, struct file *flie,int pg_num)
{
  bitmap_set_all(bitmap, true);
  return true;
}

bool check_exist_empty_region_spt(struct hash spt, void *uaddr, int pg_num){
  for(int i=0; i<pg_num; i++)
  {
    if(get_spte(spt, (uint8_t *)uaddr+PGSIZE*i)) return false;
  }
  return true;
}



struct mte *initialize_mte(int pg_num, struct file *file, int zero_bytes, void *uaddr,struct bitmap *writable_bitmap){
  struct mte *mte = malloc(sizeof(struct mte));
  mte->mapid = thread_current()->mapid++;
  mte->pg_num = pg_num;
  mte->file = file;
  mte->zero_bytes =zero_bytes;
  mte->uaddr = uaddr;
  mte->writable_bitmap = writable_bitmap;
  return mte;
}


bool creates_sptes_mmap(struct mte *mte){
  for(int i=0; i<mte->pg_num; i++){

    struct spte *spte = malloc(sizeof(struct spte));
    if(!spte) return false;

    spte -> state = MMAP_DISK;
    spte -> upage = (uint8_t *)mte->uaddr+PGSIZE*i;
    spte -> file = mte->file;
    spte -> offset = PGSIZE*i;
    if(i==mte->pg_num-1){
      spte->read_bytes = PGSIZE - mte->zero_bytes;
      spte->zero_bytes = mte -> zero_bytes;
    }
    else {
      spte ->read_bytes =PGSIZE;
      spte -> zero_bytes = 0;
    }

    spte -> evictable = false;

    if(bitmap_test(mte->writable_bitmap, i)) spte->writable =true;
    else spte->writable = false;

    if(hash_insert(&thread_current()->spt, &spte->elem)) return false;
  }

  list_push_back(&thread_current()->mte_list,&mte->elem);

  return true;
}

bool load_from_mmap(struct spte *spte){
  uint8_t *frame = frame_alloc(PAL_USER|PAL_ZERO,spte);
  if(!frame) return false;

  if(spte->read_bytes>0){
    file_seek(spte->file, spte->offset);
    if(file_read(spte->file,frame,spte->read_bytes)!=(int)spte->read_bytes){
      remove_fte(frame);
      return false;
    }
    memset(frame+spte->read_bytes,0,spte->zero_bytes);
  }
  if(!install_page(spte->upage,frame,true)){
    remove_fte(frame);
    return false;
  }
  spte->state = MMAP_MEMORY;
  spte->evictable =true;
  return true;
}


void munmap(int mapping){
  
  struct thread *cur = thread_current();
  sema_down(&cur->mte_sema);
  struct mte *mte = get_mte(cur->mte_list,mapping);
  uint8_t *frame;

  for(int i=0; i<mte->pg_num; i++)
  {
    struct spte *spte = get_spte(cur->spt, (uint8_t *)mte->uaddr+PGSIZE*i);
    if(!spte) PANIC("never");
    spte->evictable = false;
  }

  for(int i=0; i<mte->pg_num; i++)
  {
    lock_acquire(&file_lock);
    struct spte *spte = get_spte(cur->spt, (uint8_t *)mte->uaddr+PGSIZE*i);
    if(spte->state == MMAP_MEMORY){
      frame = pagedir_get_page(cur->pagedir, spte->upage);
      if(pagedir_is_dirty(cur->pagedir, spte->upage)){
        if(!write_data_mm(frame,spte->file, spte->offset, spte->read_bytes, spte->zero_bytes)) PANIC("never");
      }

      remove_fte(frame);//frame_table에서 지우고, frame  free함.
      pagedir_clear_page(cur->pagedir, spte->upage);
      hash_delete(&cur->spt, &spte->elem); //spt에서 제거
      free(spte); //spte free
    }
    else if (spte->state ==MMAP_DISK){
      //spt제거하고 spte free만 ㄱ 
      hash_delete(&cur->spt, &spte->elem); //spt에서 제거
      free(spte); //spte free
    }
    else PANIC("never occur");

    lock_release(&file_lock);
  }
  //mte mte_table에서 지워주고 free해주기. 자세한 역할은 함수 직접보기
  delete_and_free_mte(mte);
  sema_up(&cur->mte_sema);
}



struct mte *get_mte(struct list mte_list, int mapping){
  struct mte *temp = NULL;
  struct list_elem *e = list_begin(&mte_list);
  
  for (e;e!=list_end(&mte_list);e = list_next(e)){
    temp = list_entry(e, struct mte, elem);
    if(temp->mapid == mapping) break;
    else temp=NULL;
  }

  return temp!=NULL ? temp : NULL;
}


bool write_data_mm(uint8_t *frame, struct file *file, int offset, 
  int read_bytes, int zero_bytes){

  if(read_bytes==0) return true;


  file_allow_write(file);
  bool result;
  if(file_write_at(file,frame,read_bytes,offset) == (int)read_bytes) result = true;
  else result = false;
  file_deny_write(file);
  return result;
}

void delete_and_free_mte(struct mte *mte){
  list_remove(&mte->elem);
  file_close(mte->file);
  bitmap_destroy(mte->writable_bitmap);
  free(mte);
}