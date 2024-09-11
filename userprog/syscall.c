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
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "userprog/process.h"
#include "threads/synch.h"


void syscall_entry (void);
void syscall_handler (struct intr_frame *);
void halt_handler(void);
void exit_handler(int status);
bool create_handler(const char *file, unsigned initial_size);
bool remove_handler(const char *file);
int open_handler(const char *file);
int filesize_handler(int fd);
int read_handler(int fd, void *buffer, unsigned size);
int write_handler(int fd, const void *buffer, unsigned size);
void seek_handler(int fd, unsigned position);
unsigned tell_handler(int fd);
void close(int fd);

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

	lock_init(&filesys_lock);
}

//valid한 address인지 확인하는 함수, null 아닌지 / 커널에 있지 않은지 / unmap아닌지 확인
void check_validity(const uint64_t *uaddr) {
    struct thread *cur = thread_current();
    if (uaddr == NULL || is_kernel_vaddr(uaddr)||pml4_get_page(cur->pml4, uaddr)==NULL) {
        exit(-1);
    }
}

// 현재 thread fdt에서 주어진 fd 삭제
void remove_file_from_fdt(int fd) {
	struct thread *cur = thread_current();
	if(fd < 0 || fd >= FDT_COUNT_LIMIT) {
		return;
	}
	cur->fdt[fd] = NULL;
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
			break;
		case SYS_OPEN:
			f->R.rax = open_handler(f->R.rdi);
			break;
		case SYS_FILESIZE:
			f->R.rax = filesize_handler(f->R.rdi);		
			break;
		case SYS_READ:
			f->R.rax = read_handler(f->R.rdi,f->R.rsi, f->R.rdx);
			break;
		case SYS_WRITE:
			f->R.rax = write_handler(f->R.rdi,f->R.rsi, f->R.rdx);
			break;		
		case SYS_SEEK:
			seek_handler(f->R.rdi,f->R.rsi);
			break;		
		case SYS_TELL:
			f->R.rax = tell_handler(f->R.rdi);
			break;		
		case SYS_CLOSE:
			close_handler(f->R.rdi);
			break;		
		default:
			exit(-1);
			break;
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

int read_handler(int fd, void *buffer, unsigned size) {
	check_validity(buffer);
	check_validity(buffer+size-1);
	
	unsigned char *buf = buffer;
	int bytes_written;

	struct file *file = process_get_file(fd);

	char key;
	if(fd == STDIN_FILENO) {
		for(bytes_written = 0; bytes_written < size; bytes_written++) {
			key = input_getc();
			*buf++ = key;
			if(key == '\0') {
				break;
			}
		}
	} else if(fd == STDOUT_FILENO) {
		return -1;
	} else {
		// filessys lock
		lock_acquire(&filesys_lock);
		bytes_written = file_read(file, buffer, size);
		lock_release(&filesys_lock);
	}

	return bytes_written;
}

int write_handler(int fd, const void *buffer, unsigned size) {
	check_validity(buffer);
	lock_acquire(&filesys_lock);

	int ret;
	// 파일 디스크립터 테이블에서 파일 객체를 가져옴
	struct file *file = process_get_file(fd);

	// 파일 객체가 null일 경우
	if(file == NULL) {
		lock_release(&filesys_lock);
		return -1;
	} 

	if(fd == STDOUT_FILENO) {	// 표준출력인 경우
		putbuf(buffer, size);
		ret = size;		// 출력한 바이트수
	} else if(fd == STDIN_FILENO) {		// 표준입력인 경우, 쓰기 불가능하므로 오류 반환
		ret = -1;
	} else {	// 일반 파일 처리
		ret = file_write(file, buffer, size);
	}

	lock_release(&filesys_lock);

	return ret;
}

void seek_handler(int fd, unsigned position) {
	struct file *file = process_get_file(fd);
	if(file == NULL) {
		return;
	}
	if(fd <= 1) {
		return;
	}

	file_seek(file, position);
}

unsigned tell_handler(int fd) {
	struct file *file = process_get_file(fd);

	if(file == NULL) {
		return;
	}
	if(fd <= 1) {
		return;
	}

	file_tell(file);
}

void close(int fd) {
	struct file *file= process_get_file(fd);

	if(file == NULL) {
		return;
	}

	if(fd <= 1) {
		return;
	}

	remove_file_from_fdt(fd);
	file_close(file);
}