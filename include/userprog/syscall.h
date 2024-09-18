#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);
// lock 생성
struct lock filesys_lock;

void exit_handler(int status);

#endif /* userprog/syscall.h */
