#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include <list.h>
#include "vm/page.h"

struct lock frame_table_lock;
//struct lock eviction_lock;
struct list frame_table;

struct fte {
  void *frame;
  struct spte *spte; 
  struct thread *thread;
  struct list_elem elem;
};


void init_frame_table(void);
void *frame_alloc(enum palloc_flags flags, struct spte *spte);
struct fte* find_victim (void);
void create_fte(void* frame, struct spte *spte); 
void frame_table_update(struct fte *fte, struct spte *spte, struct thread *thread);
void remove_fte(void *frame);


#endif
