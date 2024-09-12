#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);

//file에 대한 race condition을 막기 위해 lock 생성
struct lock filesys_lock;

#endif /* userprog/syscall.h */
