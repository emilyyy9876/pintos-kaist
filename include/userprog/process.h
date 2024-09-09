#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

tid_t process_create_initd (const char *file_name);
tid_t process_fork (const char *name, struct intr_frame *if_);
int process_exec (void *f_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (struct thread *next);

// 
void setup_argument(char **argv_list,int argc_num, struct intr_frame *if_);

static int64_t
get_user (const uint8_t *uaddr);

#endif /* userprog/process.h */
