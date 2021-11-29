#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);
void exit_handler(int status);
struct lock file_lock;
struct file* get_file_from_fd(int fd);
#endif /* userprog/syscall.h */