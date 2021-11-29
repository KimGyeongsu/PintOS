#include "devices/block.h"
#include "threads/synch.h"
#include "userprog/process.h"
#include "userprog/syscall.h"
#include "vm/frame.h"
#include "vm/page.h"
#include "vm/swap.h"
#include <bitmap.h>


void init_swap_table(void){
  swap_block = block_get_role(BLOCK_SWAP);
  swap_table = bitmap_create(block_size(swap_block)/8);
  bitmap_set_all(swap_table, false); 
  lock_init (&swap_lock);
}


void swap_in(block_sector_t swap_index, void* frame)
{
  if(bitmap_test(swap_table, swap_index)==false)
    ASSERT("Trying to swap in a free block");

  bitmap_flip(swap_table,swap_index); //bitmap initialization

  for(int i=0; i<8; i++)
  {

    block_read(swap_block, swap_index*8+i,
      (uint8_t*)frame+i*BLOCK_SECTOR_SIZE);

  }
}


block_sector_t swap_out (void *victim){
  block_sector_t free_index = bitmap_scan_and_flip(swap_table,0,1,false); 

  if(free_index==BITMAP_ERROR) PANIC("NO free index in swap block");
  for(int i=0; i<8; i++)
  {
    block_write(swap_block,free_index*8+i, 
      (uint8_t*)victim+i*BLOCK_SECTOR_SIZE);
  }
  return free_index;
}

bool load_from_swap(struct spte* spte){
    spte->evictable = false;
    uint8_t *frame = frame_alloc (PAL_USER|PAL_ZERO, spte);
    if (!frame)
        return false;

    if (!install_page(spte->upage, frame, spte->writable)){
        remove_fte(frame);
        return false;
    }
    swap_in(spte->swap_location, frame);
    spte->state = MEMORY;
    spte->evictable = true;
    return true;
}