#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/palloc.h"
#include "userprog/syscall.h"
#include "vm/frame.h"
#include <list.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vm/swap.h"
#include "vm/mmap.h"

void init_frame_table(void);
void* frame_alloc (enum palloc_flags flags, struct spte *spte);
struct fte* find_victim (void);
void create_fte(void* frame,struct spte *spte); 
void frame_table_update(struct fte *fte, struct spte *spte, struct thread *thread);
void remove_fte(void *frame);




void init_frame_table(void){
   list_init (&frame_table);
   lock_init (&frame_table_lock);
}


void* frame_alloc (enum palloc_flags flags, struct spte *spte)
{
   if ( (flags & PAL_USER) == 0 )
        return NULL;
  
    void *frame = palloc_get_page(flags);
    if (!frame){
        struct fte *victim_fte = find_victim();
        if(victim_fte == NULL){
          return NULL;
        }
        victim_fte->spte->evictable=false;
        frame = victim_fte ->frame;

        if(victim_fte->spte->state==MEMORY) {
          victim_fte->spte->swap_location = swap_out(frame);
          update_spte(victim_fte->spte, victim_fte->thread);
        }
        else if (victim_fte->spte->state ==MMAP_MEMORY){
          //write if dirty
          if(pagedir_is_dirty(thread_current()->pagedir, victim_fte->spte->upage)){
            if(!write_data_mm(frame,victim_fte->spte->file, victim_fte->spte->offset, 
                victim_fte->spte->read_bytes, victim_fte->spte->zero_bytes)) PANIC("never");
          }
          //spte,pte update
          victim_fte->spte->state =MMAP_DISK;
          pagedir_clear_page(victim_fte->thread->pagedir, victim_fte->spte->upage);
        } 
        else PANIC("never occur");
        remove_fte(frame);
        void *new_frame = palloc_get_page(flags);
        if(!new_frame){
          return NULL;
        }
        create_fte(new_frame, spte);
    } 
    else {
        create_fte(frame, spte);    
    }
    return frame;
}



struct fte* find_victim (void) {
  struct list_elem *evict_elem;
  struct fte *victim;

  while(1)
  {
    evict_elem = list_pop_front(&frame_table);
    victim = list_entry (evict_elem, struct fte, elem);   
    list_push_back(&frame_table, evict_elem); 
    if(victim->spte->evictable==true && victim->spte->state==MEMORY) break;
    if(victim->spte->evictable==true && victim->spte->state==MMAP_MEMORY
          && !pagedir_is_dirty(thread_current()->pagedir, victim->spte->upage)) break; 
  }
  
  return list_entry (evict_elem, struct fte, elem);
}

void create_fte(void* frame, struct spte *spte)
{
   struct fte *new_fte=malloc(sizeof(struct fte));
   new_fte->frame = frame;
   new_fte->spte = spte;
   new_fte->thread = thread_current();
   list_push_back(&frame_table, &new_fte->elem);
}

void frame_table_update(struct fte *fte, struct spte *spte, struct thread *thread)
{
   fte->spte=spte;
   fte->thread=thread;
}

void remove_fte(void *frame)
{
   struct list_elem *e;
   for(e = list_begin(&frame_table); e!=list_end(&frame_table); e=list_next(e))
   {
      if(list_entry (e, struct fte, elem)->frame == frame) break;
   }
   struct fte *fte = list_entry (e, struct fte, elem);
   list_remove(&fte->elem);
   palloc_free_page(frame);
   free(fte);
}