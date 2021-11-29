#ifndef VM_MMAP_H
#define VM_MMAP_H

#include "lib/kernel/hash.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include <bitmap.h>
#include "vm/page.h"

struct mte {
	struct list_elem elem;
	int mapid; //mte id
	int pg_num; //number of page
	struct file *file; //mmap file
	int zero_bytes; //zero bytes to allocate
	void *uaddr; //virtual address start position
	struct bitmap *writable_bitmap; //writable 정보 
};

void init_mte_table(void);
//현재 thread의 mte_table 활성화, 그리고 mte_sema initialization
//process.c에서 init_spt랑 같이 해줌


int mmap(int fd, void *uaddr); //mmap function
bool setting_bitmap(struct bitmap *bitmap, struct file *flie,int pg_num);
//file의 wriatble 정보를 bitmap으로 가져옴.
bool check_exist_empty_region_spt(struct hash spt, void *uaddr, int pg_num);
//spt에 uaddr로 시작하는 연속된 pg_num개수 만큼의 spte가 없는지 확인.
struct mte *initialize_mte(int pg_num, struct file *file, 
	int zero_bytes, void *uaddr, struct bitmap *writable_bitmap);
//mte 제작. 실패시 null return 
bool creates_sptes_mmap(struct mte *mte);
//mte에 대한 spte들(state : MMAP_DISK)을 만들고, 이들을 spt에 넣음.
//그 후 mte를 mte_table에 넣어줌.
//loading 자체는 lazy loading
bool load_from_mmap(struct spte *spte);



void munmap(int mapping); //unmmap function
struct mte * get_mte(struct list mte_table, int mapping);
//mte_table에서 mapping이 mapid인 mte를 찾아서 return 없으면 null return
bool write_data_mm(uint8_t *frame, struct file *file, int offset, 
	int read_bytes, int zero_bytes);
//mmap memory 상태이고, dirty한 page일 경우, file에 내용을 덮어써줘야함.
void delete_and_free_mte(struct mte *mte);
//mte_Table에서 제거, mte->file close, mte->bitmap destroy, free mte


#endif