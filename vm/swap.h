#ifndef VM_SWAP_H
#define VM_SWAP_H

#include "threads/synch.h"
#include "devices/block.h"
#include <bitmap.h>
#include "vm/page.h"



struct block *swap_block;
struct bitmap *swap_table; 
struct lock swap_lock;


void init_swap_table(void);

void swap_in(block_sector_t swap_index, void* frame);
block_sector_t swap_out (void *victim);

bool load_from_swap(struct spte*);
#endif