#include "userprog/process.h"
#include "lib/kernel/hash.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "userprog/pagedir.h"
#include "userprog/syscall.h"
#include "vm/page.h"
#include "vm/swap.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "filesys/directory.h"



static unsigned spte_hash (const struct hash_elem *s_, void *aux UNUSED);
static bool spte_less (const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED);


/*Create hash value for hash element*/
static unsigned spte_hash (const struct hash_elem *s_, void *aux UNUSED){
    const struct spte *s = hash_entry(s_, struct spte, elem);
    return hash_bytes (&s->upage, sizeof s->upage);
}

/*Compare two hash elements, and return true if a is smaller than b*/
static bool spte_less (const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED){
    const struct spte *a = hash_entry(a_, struct spte , elem);
    const struct spte *b = hash_entry (b_, struct spte, elem);
    return a->upage < b->upage;
}

/*Initialize supplemental page table (hash table init)*/
void init_spt(struct hash *spt){
    hash_init(spt, spte_hash, spte_less, NULL);
    sema_init(&thread_current()->spt_sema, 1);
}


/*Get supplemental page table entry from given address*/
struct spte* get_spte (struct hash spt, const void *address){
    struct hash_iterator i;
    struct spte *temp=NULL;

    hash_first (&i, &spt);
    while (hash_next (&i))
    {
        temp = hash_entry (hash_cur (&i), struct spte, elem);
        if(temp->upage==address) break;
        else temp=NULL;
    }
    return temp != NULL ? temp : NULL;
}

bool create_spte(const void *addr, const enum status status, const bool writable,const bool evictable)
{
    struct spte *spte = malloc(sizeof(struct spte));
    if (!spte)  return false;

    spte-> state = status;
    spte -> upage = addr;
    spte->writable = writable;
    spte->evictable = evictable;
    spte->swap_location = NULL;

    bool result = (hash_insert(&thread_current()->spt, &spte->elem)==NULL);

    return result;
}

/*Update the supplemental page table and page table with new address of pages*/
void update_spte(struct spte * spte, struct thread *thread)
{
    //spte update
    spte->state = SWAP_DISK;
    //pte update
    pagedir_clear_page(thread->pagedir, spte->upage);
}

/*Destroy and free all the resources allocated for supplemental page table*/
void destroy_spt(void){ 
    hash_destroy(&thread_current()->spt, free);
}


bool stack_growth(const void *uaddr)
{
    /*create spte for new page for stack*/
    struct spte *spte = malloc(sizeof(struct spte));
    if (!spte)  return false;
    spte-> state = MEMORY;
    spte -> upage = pg_round_down(uaddr);
    spte -> writable = true;
    spte -> evictable = false;

    bool result = (hash_insert(&thread_current()->spt, &spte->elem)==NULL);
    if(!result) return false;
    /*frame allocation*/
    uint8_t *frame=frame_alloc(PAL_USER|PAL_ZERO, spte);
    if(!frame) return false;
    /*install page*/
    if(!install_page(spte->upage, frame, spte->writable)){
        remove_fte(frame);
        return false;
    }
    spte -> evictable = true;
    return true;
}
//3-2
bool load_from_exec(struct spte *spte)
{
    spte->evictable = false;
    uint8_t *frame = frame_alloc(PAL_USER|PAL_ZERO, spte);
    if(!frame) return false;

    if(spte->read_bytes>0){

        file_seek(spte->file, spte->offset);
        if(file_read (spte->file, frame, spte->read_bytes) != (int) spte->read_bytes)
        {
            remove_fte(frame);
            return false;
            
        }
        memset(frame+spte->read_bytes,0,spte->zero_bytes);
    }

    if(!install_page(spte->upage,frame,spte->writable)){
        remove_fte(frame);
        return false;
    }
    spte->state = MEMORY;
    //if lazy loading is done, close the file.
    if(!(--thread_current()->file_close_cnt)) {
        file_close(spte->file);
    }
    if(thread_current()->file_close_cnt<0) PANIC("error_load_from_exec");
    spte->evictable =true;
    return true;
}


bool create_spte_from_exec(struct file *file, off_t ofs, 
                                uint8_t *upage, uint32_t read_bytes, uint32_t zero_bytes, bool writable){
    struct spte *spte = malloc(sizeof(struct spte));
    if (!spte)  return false;

    spte->upage = upage;
    spte->file =file;
    spte->state= EXEC_FILE;

    spte->offset = ofs;
    spte->read_bytes = read_bytes;
    spte->zero_bytes = zero_bytes;

    spte->writable = writable;

    bool result = (hash_insert(&thread_current()->spt, &spte->elem)==NULL);
    return result;
}
