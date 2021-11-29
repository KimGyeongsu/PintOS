#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/exception.h"
#include "devices/shutdown.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/parse.h"
#ifdef VM
#include "vm/frame.h"
#include "vm/page.h"
#include "vm/swap.h"
#include "vm/mmap.h"
#endif

static void syscall_handler (struct intr_frame *);

static void halt_handler (void);
void exit_handler (int status);
static int exec_handler (const char *file);
static int wait_handler (int pid);
static bool create_handler (const char *file, unsigned initial_size);
static bool remove_handler (const char *file);
static int open_handler (const char *file);
static int filesize_handler (int fd);
static int read_handler (int fd, void *buffer, unsigned size);
static int write_handler (int fd, const void *buffer, unsigned size);
static void seek_handler (int fd, unsigned position);
static unsigned tell_handler (int fd);
static void close_handler (int fd);
static int mmap_handler(int fd, void *addr);
static void munmap_handler(int mapping);

static bool chdir_handler(const char *dir);
static bool mkdir_handler(const char *dir);
static bool readdir_handler(int fd, char *name);
static bool isdir_handler(int fd);
static int inumber_handler(int fd);

static void is_valid_addr_1(const void *vaddr);
static void is_valid_addr_2(const void *vaddr_a, const void *vaddr_b);
static void is_valid_addr_3(const void *vaddr_a, const void *vaddr_b,const void *vaddr_c);

struct file* get_file_from_fd(int fd);

#ifdef VM
static int32_t get_user(const uint8_t *uaddr);
#endif
void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init(&file_lock);
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  uint32_t syscall_number = *(uint32_t *)(f->esp);
  //printf("number is %d\n",syscall_number);
#ifdef VM

  if(syscall_number<0 || syscall_number>19) exit_handler(-1);
  thread_current()->saved_esp = f->esp;

#endif
  switch(syscall_number)
  {
    case SYS_HALT :
     // printf("tell_us_where_halt\n");
      halt_handler();
      break;
    case SYS_EXIT :
      is_valid_addr_1((uint32_t *)(f->esp+4));
      //printf("tell_us_where_exit\n");
      exit_handler(*(uint32_t *)(f->esp+4));
      break;
    case SYS_EXEC :
      is_valid_addr_1((uint32_t *)(f->esp+4));
      //printf("tell_us_where_exec\n");
      lock_acquire(&file_lock);
      f->eax=exec_handler(*(uint32_t *)(f->esp+4));
      lock_release(&file_lock);
      break;
    case SYS_WAIT:
      is_valid_addr_1((uint32_t *)(f->esp+4));
      //printf("tell_us_where_wait\n");
      f->eax=wait_handler(*(uint32_t *)(f->esp+4));
      break;
    case SYS_CREATE:
      is_valid_addr_2((uint32_t *)(f->esp+4),(uint32_t *)(f->esp+8));
      //printf("tell_us_where_create\n");
      lock_acquire(&file_lock);
      f->eax=create_handler(*(uint32_t *)(f->esp+4),*(uint32_t *)(f->esp+8));
      lock_release(&file_lock);
      break;
    case SYS_REMOVE:
      is_valid_addr_1((uint32_t *)(f->esp+4));
      //printf("tell_us_where_remove\n");
      lock_acquire(&file_lock);
      f->eax=remove_handler(*(uint32_t *)(f->esp+4));
      lock_release(&file_lock);
      break;
    case SYS_OPEN:
      is_valid_addr_1((uint32_t *)(f->esp+4));
      //printf("tell_us_where_open\n");
      lock_acquire(&file_lock);
      f->eax=open_handler(*(uint32_t *)(f->esp+4));
      lock_release(&file_lock);
      break;
    case SYS_FILESIZE :
      is_valid_addr_1((uint32_t *)(f->esp+4));
      //printf("tell_us_where_filesize\n");
      lock_acquire(&file_lock);
      f->eax=filesize_handler(*(uint32_t *)(f->esp+4));
      lock_release(&file_lock);
      break;
    case SYS_READ :
      is_valid_addr_3((uint32_t *)(f->esp+4),(uint32_t *)(f->esp+8),(uint32_t *)(f->esp+12));
      //printf("tell_us_where_read\n");
      f->eax=read_handler(*(uint32_t *)(f->esp+4),*(uint32_t *)(f->esp+8), *(uint32_t *)(f->esp+12));
      break;
    case SYS_WRITE :
      is_valid_addr_3((uint32_t *)(f->esp+4),(uint32_t *)(f->esp+8),(uint32_t *)(f->esp+12));
      //printf("tell_us_where_write\n");
      f->eax=write_handler(*(uint32_t *)(f->esp+4), *(uint32_t *)(f->esp+8), *(uint32_t *)(f->esp+12));
      break;
    case SYS_SEEK:
      //printf("validation seeek before\n");
      is_valid_addr_2((uint32_t *)(f->esp+4),(uint32_t *)(f->esp+8));
      //printf("tell_us_where_seek\n");
      lock_acquire(&file_lock);
      seek_handler(*(uint32_t *)(f->esp+4),*(uint32_t *)(f->esp+8));
      lock_release(&file_lock);
      break;
    case SYS_TELL:
      is_valid_addr_1((uint32_t *)(f->esp+4));
      //printf("tell_us_where_tell\n");
      f->eax=tell_handler(*(uint32_t *)(f->esp+4));
      break;
    case SYS_CLOSE:
      is_valid_addr_1((uint32_t *)(f->esp+4));
      //printf("tell_us_where_close\n");
      lock_acquire(&file_lock);
      close_handler(*(uint32_t *)(f->esp+4));
      lock_release(&file_lock);
      break;
#ifdef VM
    case SYS_MMAP:
      is_valid_addr_2((uint32_t *)(f->esp+4),(uint32_t *)(f->esp+8));
      lock_acquire(&file_lock);
      f->eax=mmap_handler(*(uint32_t *)(f->esp+4),*(uint32_t *)(f->esp+8));
      lock_release(&file_lock);
      break;
    case SYS_MUNMAP:
      is_valid_addr_1((uint32_t *)(f->esp+4));
      munmap_handler(*(uint32_t *)(f->esp+4));
      break;
#endif

    case SYS_CHDIR:
      is_valid_addr_1((uint32_t *)(f->esp+4));
    //  printf("tell_us_where_chdir\n");
      lock_acquire(&file_lock);
      f->eax=chdir_handler(*(uint32_t *)(f->esp+4));
      lock_release(&file_lock);
      break;
    case SYS_MKDIR:
      is_valid_addr_1((uint32_t *)(f->esp+4));
      //printf("tell_us_where_mkdir\n");
      lock_acquire(&file_lock);
      f->eax=mkdir_handler(*(uint32_t *)(f->esp+4));
      lock_release(&file_lock);
      break;
    case SYS_READDIR:
      is_valid_addr_2((uint32_t *)(f->esp+4),(uint32_t *)(f->esp+8));
      //printf("tell_us_where_readdir\n");
      lock_acquire(&file_lock);
      f->eax=readdir_handler(*(uint32_t *)(f->esp+4),*(uint32_t *)(f->esp+8));
      lock_release(&file_lock);
      break;
    case SYS_ISDIR:
      is_valid_addr_1((uint32_t *)(f->esp+4));
      //printf("tell_us_where_isdir\n");
      lock_acquire(&file_lock);
      f->eax=isdir_handler(*(uint32_t *)(f->esp+4));
      lock_release(&file_lock);
      break;
    case SYS_INUMBER:
      is_valid_addr_1((uint32_t *)(f->esp+4));
      //printf("tell_us_where_inumber\n");
      lock_acquire(&file_lock);
      f->eax=inumber_handler(*(uint32_t *)(f->esp+4));
      lock_release(&file_lock);
      break;
    default:
      exit_handler(-1);
  }
}


static void
halt_handler (void) 
{
   shutdown_power_off();
}


void
exit_handler (int status)
{

  if(lock_held_by_current_thread(&file_lock)) lock_release(&file_lock);

  struct thread *t = thread_current();
  t->exit_status = status;



#ifdef VM
  lock_acquire(&file_lock);
  
  if(t->execute_file && t->file_close_cnt!=0) {
    printf("syscall called file close from tid %d\n", t->tid);
    file_close(t->execute_file);
  }
#endif

  for(int i=2;i<=t->max_fd;i++)
  {
    if(t->file_list[i]==NULL) continue;
    close_handler(i);
  }

#ifdef VM
  lock_release(&file_lock);
#endif
  palloc_free_page(t->file_list);

  printf("%s: exit(%d)\n",t->name, status);
  thread_exit();
  
}


static int
exec_handler (const char *cmd_line)
{
  is_valid_addr_1(cmd_line);

  int result = process_execute(cmd_line);
  return result;
}

static int
wait_handler (int pid)
{
  return process_wait(pid);
}

static bool
create_handler (const char *file, unsigned initial_size)
{
  is_valid_addr_1(file);
  if(file==NULL) exit_handler(-1);

  bool result = filesys_create(file, initial_size);
  return result;
}

static bool
remove_handler (const char *file)
{
  is_valid_addr_1(file);
  bool result = filesys_remove(file);
  return result;
}

static int
open_handler (const char *file)
{

  is_valid_addr_1(file);
  struct thread *t = thread_current();
  struct file * open_file = filesys_open(file);
  //printf("file->inode->sector %d previous sector %d\n", open_file->inode->sector, open_file->inode->prev_sector);
  if(open_file==NULL)
  {
    return -1;
  }

  if(strcmp(t->name, file)==0) file_deny_write(open_file);
  t->max_fd +=1;
  int fd = t->max_fd;
  t->file_list[fd]=open_file;
  return fd;
}

static int
filesize_handler (int fd) 
{
  struct file *open_file = get_file_from_fd(fd);
  if(open_file == NULL) return -1;
  int result = file_length(open_file);
  return result;
}


#ifdef VM

static bool vm_is_valid_addr_1(void * uaddr);

static int
read_handler (int fd, void *buffer, unsigned size) 
{
  unsigned size0 = size;
  void *buffer0 = buffer;
  int read_size = 0;


  while(size0>0){
    is_valid_addr_1(buffer0);
    if(get_user(buffer0)==-1) exit_handler(-1);

    sema_down(&thread_current()->spt_sema);
    //lock_acquire(&file_lock); 

    unsigned offset = size0 < (PGSIZE - pg_ofs(buffer0)) ? size0 : (PGSIZE - pg_ofs(buffer0));

    struct spte *spte=NULL;
    if((spte=get_spte(thread_current()->spt,pg_round_down(buffer0)))==NULL) {
      sema_up(&thread_current()->spt_sema);
      //lock_release(&file_lock);
      exit_handler(-1);
    }
    spte->evictable=false;

    if(fd==0)
    {
      for(int i=0; i<offset; i++)
      {
        *(uint8_t *)(buffer0+i)=input_getc();
      }

      spte->evictable=true;
      size0 -= offset;
      buffer0 = pg_round_down(buffer0)+PGSIZE;
      sema_up(&thread_current()->spt_sema);
      //lock_release(&file_lock);
      continue;
    }
    else
    {
      struct file *open_file = get_file_from_fd(fd);
      if(open_file==NULL)
      {
        spte->evictable=true;
        sema_up(&thread_current()->spt_sema);
        //lock_release(&file_lock);
        return -1;
      }
      read_size += file_read(open_file,buffer0,offset);
      spte->evictable=true;
      size0 -= offset;
      buffer0 = pg_round_down(buffer0)+PGSIZE;
      sema_up(&thread_current()->spt_sema);
      //lock_release(&file_lock);
    }
  }
  if(fd==0) return size;
  else return read_size;
}   



static int
write_handler (int fd, const void *buffer, unsigned size) 
{
  //printf("aaa\n");
  if(isdir_handler(fd)) return -1;
  //printf("bbb\n");
  unsigned size0 = size;
  void *buffer0 = buffer;
  int write_size = 0;

  while(size0>0){
    is_valid_addr_1(buffer0);
    if(get_user(buffer0)==-1) exit_handler(-1);
    sema_down(&thread_current()->spt_sema);
    //lock_acquire(&file_lock);
    unsigned offset = size0 < (PGSIZE - pg_ofs(buffer0)) ? size0 : (PGSIZE - pg_ofs(buffer0));
    struct spte *spte=NULL;
    if((spte=get_spte(thread_current()->spt,pg_round_down(buffer0)))==NULL) {

      sema_up(&thread_current()->spt_sema);
      //lock_release(&file_lock); 
      exit_handler(-1);
    }
    spte->evictable=false;

    if(fd==1)
    {
      //printf("aaaaaa\n");
      putbuf(buffer0,offset);
      spte->evictable=true;
      size0 -= offset;
      buffer0 = pg_round_down(buffer0)+PGSIZE;
      sema_up(&thread_current()->spt_sema);
      //lock_release(&file_lock);
      continue;
    }
    else
    {
      struct file *open_file = get_file_from_fd(fd);
      if(open_file==NULL)
      {
        spte->evictable=true;
        sema_up(&thread_current()->spt_sema);
        //lock_release(&file_lock);
        return -1;
      }
      //lock_acquire(&file_lock);
      write_size += file_write(open_file,buffer0,offset);
      //lock_release(&file_lock);
      spte->evictable=true;
      size0 -= offset;
      buffer0 =pg_round_down(buffer0)+PGSIZE;
      sema_up(&thread_current()->spt_sema);
      //lock_release(&file_lock);
    }
  }
  if(fd==1) return size;
  else return write_size;
}

#endif

#ifndef VM
static int
read_handler (int fd, void *buffer, unsigned size) 
{
  is_valid_addr_1(buffer);
  lock_acquire(&file_lock);
  if(fd==0)
  {
    for(int i=0; i<size; i++)
    {
      *(uint8_t *)(buffer+i)=input_getc();
    }
    lock_release(&file_lock);
    return size;
  }
  int read_size=0;

  struct file *open_file = get_file_from_fd(fd);

  if(open_file==NULL)
  {
    lock_release(&file_lock);
    return -1;
  }

  read_size = file_read(open_file,buffer,size);
  lock_release(&file_lock); 
  return read_size;
}   


static int
write_handler (int fd, const void *buffer, unsigned size)
{
  is_valid_addr_1(buffer);
  lock_acquire(&file_lock);
  int write_size;

  if(fd==1)
  {
    putbuf(buffer,size);
    lock_release(&file_lock);
    return size;
  }

  struct file *open_file = get_file_from_fd(fd);
  if(open_file==NULL)
  {
    lock_release(&file_lock);
    return -1;
  }
  write_size = file_write(open_file,buffer,size);
  lock_release(&file_lock);
  return write_size;
}
#endif

static void
seek_handler (int fd, unsigned position) 
{
  struct file *open_file = get_file_from_fd(fd);
  if(open_file==NULL) exit_handler(-1);
  file_seek(open_file, position); 
}

static unsigned
tell_handler (int fd) 
{
  struct file *open_file = get_file_from_fd(fd);
  if(open_file==NULL) exit_handler(-1);
  unsigned result = file_tell(open_file);
  return result;
}

static void
close_handler (int fd)
{
  struct file *open_file = get_file_from_fd(fd);
  if(open_file==NULL) exit_handler(-1);
  //printf("file->inode->sector %d previous sector %d\n", open_file->inode->sector, open_file->inode->prev_sector);
  thread_current()->file_list[fd]=NULL;
  file_close(open_file);
}

#ifdef VM

static int mmap_handler(int fd, void *addr){
  if(!is_user_vaddr(addr)) exit_handler(-1);

  sema_down(&thread_current()->spt_sema);
  sema_down(&thread_current()->mte_sema);
  lock_acquire(&swap_lock);
  lock_acquire(&frame_table_lock);

  int result = mmap(fd, addr);

  lock_release(&swap_lock);
  lock_release(&frame_table_lock);
  sema_up(&thread_current()->spt_sema);
  sema_up(&thread_current()->mte_sema);
  return result;
}

static void munmap_handler(int mapping){

  sema_down(&thread_current()->spt_sema);
  lock_acquire(&swap_lock);
  lock_acquire(&frame_table_lock);

  munmap(mapping);

  lock_release(&swap_lock);
  lock_release(&frame_table_lock);
  sema_up(&thread_current()->spt_sema);
}


#endif

static bool chdir_handler(const char *dir)
{
  char *temp = malloc(strlen(dir)+1);
  strlcpy(temp, dir, strlen(dir)+1);
  struct parsing_result *result = parse(temp, false); 

  if(result->valid==false || result->is_dir==false)  //not valid and it is not directory
  {
    free(result);
    free(temp);
    return false;
  }

  struct dir *directory;
  struct inode *inode = NULL;


  if(result->is_root ==true){ //case(1) root directory
    thread_current()->cur_dir_sector = 1;
    free(result);
    free(temp);
    return true;
  }

  directory = dir_open(result->dir_inode);
  if(dir_lookup(directory, result->filename, &inode)!=true) PANIC("ERROR\n");
  thread_current()->cur_dir_sector = inode->sector;
  inode_close(inode);
  dir_close(directory);
  free(result);
  free(temp);
  return true;
}

static bool mkdir_handler(const char *dir)
{
  char *temp = malloc(strlen(dir)+1);
  strlcpy(temp, dir, strlen(dir)+1);
  struct parsing_result *result = parse_mkdir(temp); 
  if(result->valid==false) 
  {
    free(result);
    free(temp);
    return false;
  }
  if(result->is_root ==true){ //case(1) root directory
    free(result);
    free(temp);
    return false;
  }
  if(result->is_dir==false) //case (2) this is file
  {
    PANIC("NEVER OCCUR\n");
    free(result);
    free(temp);
    return false;
  }

  block_sector_t inode_sector = 0;
  struct dir *dir0 = dir_open(result->dir_inode);
  bool success;
  struct dir *new_dir;

  success = (dir0 != NULL
                  && free_map_allocate (1, &inode_sector) 
                  && inode_create (inode_sector, 320, dir0->inode->sector, true)
                  && dir_add (dir0, result->filename, inode_sector));
  //printf("my directory sector %d\n",inode_sector);
 /* struct dir_entry e;
  size_t ofs;

  for (ofs = 0; inode_read_at (dir0->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    //printf("inode_sector %d, name %s, in_use %d\n",e.inode_sector, e.name,e.in_use);
    if (e.in_use) //열려있고, 이름이 같을 때 
      //strcmp : 같으면 0 반환
      {
        printf("name %s and sector %d\n", e.name,e.inode_sector);
      }//*/

  new_dir = dir_open(inode_open(inode_sector));
  dir_add (new_dir, ".", inode_sector);
  dir_add (new_dir, "..",dir0->inode->sector);
  dir_close(new_dir);


  if (!success && inode_sector != 0)
  {
    free_map_release (inode_sector, 1);
  }

  dir_close (dir0);
  free(result);
  free(temp);
  return success;
}

static bool readdir_handler(int fd, char *name)
{
  if (!isdir_handler(fd)) return false; //fd is must directory
  //printf("name is %s\n", name);
  struct file *open_file = get_file_from_fd(fd);
  //printf("bbb\n");
  struct dir *dir=dir_open(inode_open(open_file->inode->sector)); //directory
  bool result =  dir_readdir(dir, name);
  dir_close(dir);
  return result;
}

static bool isdir_handler(int fd)
{
  struct file *open_file = get_file_from_fd(fd);
  if(open_file==NULL) return false;
  return open_file->inode->is_dir;
}
static int inumber_handler(int fd)
{
  //printf("enter_INUMBER\n");
  struct file *open_file = get_file_from_fd(fd);
  if(open_file==NULL) return false;
  return open_file->inode->sector;
}






#ifdef VM
static int32_t get_user(const uint8_t *uaddr)
{
  int result;
  asm("movl $1f, %0; movzbl %1, %0; 1:"
      : "=&a" (result) : "m" (*uaddr));
  return result;
}
#endif


static void is_valid_addr_1(const void *vaddr)
{
#ifdef VM
  if(!is_user_vaddr(vaddr) || vaddr==NULL) exit_handler(-1);
  return;
#endif
  if(!is_user_vaddr(vaddr) || vaddr==NULL) exit_handler(-1);
  void *page = pagedir_get_page(thread_current()->pagedir, vaddr);
  if(page==NULL) exit_handler(-1);
}

static void is_valid_addr_2(const void *vaddr_a, const void *vaddr_b)
{
  is_valid_addr_1(vaddr_a);
  is_valid_addr_1(vaddr_b);
}

static void is_valid_addr_3(const void *vaddr_a, const void *vaddr_b,const void *vaddr_c)
{
  is_valid_addr_1(vaddr_a);
  is_valid_addr_1(vaddr_b);
  is_valid_addr_1(vaddr_c);
}



struct file* get_file_from_fd(int fd)
{
   struct thread * t = thread_current();
   if(t->max_fd <fd) return NULL;
   struct file * open_file = t->file_list[fd];
   return open_file;
}