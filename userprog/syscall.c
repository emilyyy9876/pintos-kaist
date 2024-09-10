#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

/* project 2 */
#include "include/filesys/filesys.h"
#include "include/userprog/process.h"
#include "include/filesys/file.h"


void syscall_entry (void);
void syscall_handler (struct intr_frame *);
void halt_handler(void);
void exit_handler(int status);
bool create_handler(const char *file, unsigned initial_size);
bool remove_handler(const char *file);
int open_handler(const char *file);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

//valid한 address인지 확인하는 함수, null 아닌지 / 커널에 있지 않은지 / unmap아닌지 확인
void check_validity(const uint64_t *uaddr) {
    struct thread *cur = thread_current();
    if (uaddr == NULL || is_kernel_vaddr(uaddr)||pml4_get_page(cur->pml4, uaddr)==NULL) {
        exit(-1);
    }
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
	// syscall  호출 시 enum확인 및 케이스 별 생
	switch (f->R.rax) {
		case SYS_HALT:
			halt_handler();
			break;
		case SYS_EXIT:
			exit_handler(f->R.rdi);
			break;
		case SYS_CREATE:
			f->R.rax = create_handler(f->R.rdi, f->R.rsi);
			break;
		case SYS_REMOVE:
			f->R.rax = remove_handler(f->R.rdi);
		case SYS_OPEN:
			f->R.rax = open_handler(f->R.rdi);
		case SYS_FILESIZE:
			f->R.rax = filesize_handler(f->R.rdi);		
		
	}
}

void halt_handler(void) {
	power_off();
}

void exit_handler(int status) {
	struct thread *curr = thread_current();
	curr->exit_status = status;
	thread_exit();
	printf("%s: exit(%d)", curr->name, curr->exit_status);
}

bool create_handler(const char *file, unsigned initial_size) {
	check_validity(file);
	filesys_create(file, initial_size);
}

bool remove_handler(const char *file) {
	check_validity(file);
	filesys_remove(file);
}

int open_handler(const char *file) {
	check_validity(file);
	struct file *open_file = filesys_open(file);

	if(file == NULL) {
		return -1;
	}

	int fd = process_add_file(open_file);
	
	// 실패했을 때 파일을 닫아주기
	if(fd == -1) {
		file_close(open_file);
	}

	return fd;
}

int filesize_handler(int fd) {
	struct file *file = process_get_file(fd);

	if(file == NULL) {
		return -1;
	}
	
	file_length(file);
}

int read(int fd, void *buffer, unsigned size) {
	
}


