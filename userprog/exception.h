#ifndef USERPROG_EXCEPTION_H
#define USERPROG_EXCEPTION_H

#ifdef VM
#define USER_VADDR_BOTTOM 0x08048000
#define STACK_HEURISTIC 32
#define MAX_STACK_GROWTH 8*1024*1024
#endif

/* Page fault error code bits that describe the cause of the exception.  */
#define PF_P 0x1    /* 0: not-present page. 1: access rights violation. */
#define PF_W 0x2    /* 0: read, 1: write. */
#define PF_U 0x4    /* 0: kernel, 1: user process. */

void exception_init (void);
void exception_print_stats (void);

#endif /* userprog/exception.h */
