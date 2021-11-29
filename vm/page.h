#ifndef VM_PAGE_H
#define VM_PAGE_H

#include "lib/kernel/hash.h"
#include "threads/thread.h"
#include "devices/block.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/off_t.h"
#include "filesys/directory.h"


enum status {
    EXEC_FILE,SWAP_DISK, MEMORY , MMAP_DISK, MMAP_MEMORY
};

struct spte {
    struct hash_elem elem;

    enum status state;
    void *upage;

    //for lazy loading
    struct file *file;
    off_t offset;
    uint32_t read_bytes;
    uint32_t zero_bytes;



    bool writable;

    bool evictable;

    block_sector_t swap_location;
};


void init_spt(struct hash* spt);
struct spte* get_spte(struct hash spt,const void *); 
bool create_spte(const void *, const enum status, const bool writable, const bool evictable);
void update_spte(struct spte *, struct thread *);
void destroy_spt(void);
bool stack_growth(const void *);
//3-2
bool load_from_exec(struct spte *);
bool create_spte_from_exec( struct file *file, off_t ofs, 
                uint8_t *upage, uint32_t read_bytes, uint32_t zero_bytes,bool writable);


/*Supplemental page table*/

#endif