#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "filesys/filesys.h"
//-----------------project2에서 추가------------------
#include "filesys/file.h"
#include "threads/synch.h"
#include "user/syscall.h"
#include "userprog/process.h"
#include "threads/palloc.h"
#include "threads/init.h"
#include "devices/input.h"
#include "lib/string.h"



void syscall_entry (void);
void syscall_handler (struct intr_frame *);

//--------------------project2에서 추가---------------
int put_file_to_fdt(struct file *file);
struct file *get_file_by_fd(int fd);
void delete_fd_from_fdt(int dt);

void check_addr_validity(const void *uaddr);
void halt (void);
void exit (int status);
pid_t fork (const char *thread_name);

int exec (const char *cmd_line);
bool create(const char *file, unsigned inital_size);
bool remove(const char *file);
int open(const char *file);
int filesize(int fd);
int read(int fd, void *buffer, unsigned size);
int write(int fd, const void *buffer, unsigned size);
void seek(int fd, unsigned position);
unsigned tell(int fd);
void close(int fd);
int put_file_to_fdt(struct file *file);
struct file *get_file_by_fd(int fd);
void delete_fd_from_fdt(int fd);

//----------------------------------------------------

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

	// filesys_lock초기화
	lock_init(&filesys_lock);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
	
	uint64_t number= f->R.rax;
	struct thread *curr = thread_current();

	switch(number) {
		//process 관련 syscall
		case SYS_HALT:
			halt();
			break;
		case SYS_EXIT:
			exit(f->R.rdi);
			break;
		case SYS_FORK:
			f->R.rax=fork(f->R.rdi);
			break;
		case SYS_EXEC:
			f->R.rax = exec(f->R.rdi);
			break;
		case SYS_WAIT:
			f->R.rax = wait(f->R.rdi);
			break;

		//file 관련 syscall
		case SYS_CREATE:
			f->R.rax = create(f->R.rdi, f->R.rsi);
			break;
		case SYS_REMOVE:
			f->R.rax = remove(f->R.rdi);
			break;
		case SYS_OPEN:
			f->R.rax = open( f->R.rdi);
			break;
		case SYS_FILESIZE:
			f->R.rax = filesize(f->R.rdi);
			break;
		case SYS_READ:
			f->R.rax = read(f->R.rdi,f->R.rsi, f->R.rdx);
			break;
		case SYS_WRITE:
			f->R.rax = write(f->R.rdi,f->R.rsi, f->R.rdx);
			break;
		case SYS_SEEK:
			seek(f->R.rdi,f->R.rsi);
			break;
		case SYS_TELL:
			f->R.rax = tell(f->R.rdi);
			break;
		case SYS_CLOSE:
			close(f->R.rdi);
			break;
		default:
			exit(-1);
			break;
	}
	
}

//valid한 address인지 확인하는 함수
void check_addr_validity(const void *uaddr) {
	struct thread *cur = thread_current();
	if (uaddr == NULL || is_kernel_vaddr(uaddr)||pml4_get_page(cur->pml4, uaddr)==NULL) {

		exit(-1);//? 맞음??
	}
	
}

void halt (void) {
	power_off();
}

void exit (int status) {
	// exit process
	// It should print message “Name of process: exit(status)”.
	struct thread* cur = thread_current();
	cur->exit_status= status;
	printf("%s:exit(%d)\n", cur->name, status);
	thread_exit();//process_exit?
	//sema up & down???
}

//Create new process which is the clone of current process with the name THREAD_NAME
pid_t fork (const char *thread_name) {
	struct thread *curr = thread_current();
	return process_fork(thread_name,&curr->tf);
	}

// 현재 프로세스를 cmd_line에 주어진 실행 파일로 변경하고, 필요한 인수를 전달합니다.
// 이 함수는 성공한 경우 반환하지 않습니다.
// 프로그램이 로드되거나 실행될 수 없는 경우 종료 상태 -1로 프로세스가 종료됩니다.
// 이 함수는 exec를 호출한 스레드의 이름을 변경하지 않습니다.
// 파일 디스크립터는 exec 호출을 통해 변경되지 않고 열린 상태를 유지합니다.
// (exec 함수를 호출하여 프로세스를 새로운 실행 파일로 대체하더라도, 이미 열려있는 파일 디스크립터들은 그대로 유지됩니다.
int exec (const char *cmd_line) {
	check_addr_validity(cmd_line);
	char *cmd_line_copy;

	cmd_line_copy = palloc_get_page(0);
	if (cmd_line_copy==NULL)
		exit(-1);

	strlcpy(cmd_line_copy, cmd_line, PGSIZE);

	if (process_exec(cmd_line_copy) == -1) {
		palloc_free_page(cmd_line_copy);//?
		exit(-1);
	}
	NOT_REACHED();
	return 0;

}
int wait(int pid) {
	return process_wait(pid);
}

//process 관련 syscall
bool create(const char *file, unsigned inital_size) {
	check_addr_validity(file);
	return filesys_create(file, inital_size);
}

bool remove(const char *file) {
	check_addr_validity(file);
	return filesys_remove(file);
}

int open(const char *file) {
	check_addr_validity(file);
	
	struct file *f = filesys_open(file);//.....fd/null이 반환됨
	if (f==NULL) {
		return -1;
	}
	//1. 만들어진 파일 식별자를 프로세스에게 부여하기
	//2. 파일 식별자를 자식 프로세스에게 전달하기(x)---->fork할 때 구현됨
	int fd = put_file_to_fdt(f);
	if (fd == -1) {
		file_close(f);
		return -1;
	}
	return fd;
}

// Return the size, in bytes, of the file open as fd.
int filesize(int fd) {
	//1. fdt에서 fd를 이용해서 file 포인터를 찾기
	//2. file포인터를 file_length()에 넣기
	struct file *f = get_file_by_fd(fd);
	//만약 f가 null이면???
	if(f==NULL) {
		return -1;
	}
	return file_length(f);
}

int read(int fd, void *buffer, unsigned size) {
	lock_acquire(&filesys_lock);

	int ret=0;
	struct file *f = get_file_by_fd(fd);
	
	//유효한 버퍼주소인지 확인하기
	check_addr_validity(buffer);

	//유효한 fd인지 확인
	if(fd>FDCOUNT_LIMIT || fd <0 || fd==1){
		printf("read fail: invalid fd");
		lock_release(&filesys_lock);
		return -1;
	}
	//fd가 0인경우 --->키보드 입력받기
	else if (fd==0) { 
		unsigned char *buf = buffer;
		for(int i=0;i<(int)size;i++) {
			char c=input_getc();
			*buf++=c;//buf가 가리키는 위치에 c의 값을 복사하기 //버퍼 포인터를 1 증가시킴
			if (c=='\0') {
				break;
			}
			ret=i;
		}
	}
	//fd가 0이 아닌 경우--->파일읽기
	else {
		ret = file_read(f,buffer,size);
	}
	lock_release(&filesys_lock);
	return ret;
}

int write(int fd, const void *buffer, unsigned size) {
	check_addr_validity(buffer);

	int ret;
	struct file *f = get_file_by_fd(fd);

	//buffer 주소가 유효한지 확인하기
	lock_acquire(&filesys_lock);

	//유효한 buffer인지 확인하기(fd=0인 경우 포함)
	if(fd>FDCOUNT_LIMIT||fd<0||fd==0) {
		printf("write fail: invalid fd");
		lock_release(&filesys_lock);
		return -1;
	}
	//fd 1인 경우-------->console에 쓰기
	if (fd == 1) {
		putbuf(buffer, size);		/* to print buffer strings on the display*/
		ret = size;
		
	}
	//fd가 그 외인 경우 --------->file에 쓰기
	else {
		ret = file_write(f,buffer,size);
	}
	lock_release(&filesys_lock);
	return ret;

}

void seek(int fd, unsigned position) {
	struct file *f = get_file_by_fd(fd);

	if(fd<=1||f==NULL) {
		return;
	}

	file_seek(f, position);
}

unsigned tell(int fd) {
	struct file *f = get_file_by_fd(fd);

	if(fd<=1||f==NULL) {
		return -1;
	}

	return file_tell(f);
}

//파일 식별자 fd를 닫음 --->open 거꾸로(fdt에서 fd 삭제)
void close(int fd) {
	//1. fdt에서 fd의,주소를 찾기
	//2. 찾아서 인자로 전달하기
	//------->get_file_by_fd()이용
	struct file *f = get_file_by_fd(fd);

	if (f==NULL||fd<=1) {
		return;
	}

	delete_fd_from_fdt(fd);

	file_close(f);
}


//-------------------project_2---------------------
int put_file_to_fdt(struct file *file) {
	struct thread *cur = thread_current();
	struct file **fdt = cur->fdt;
	int fd_num=2;
	
	//if문 쓰면 안되는 이유.....remove되면, 빈 인덱스 생김
	// if (next_idx >=64) {
	// 	return -1;
	// };

	//while문 돌면서, 빈 인덱스 찾기
	while(fd_num<FDCOUNT_LIMIT && fdt[fd_num]) {
		fd_num++;
	}

	//최대 파일 디스크립터 수를 넘으면 실패...-> 빈 자리 나면 그 이후엔 채울 수 있게 해야 함?
	if (fd_num>=FDCOUNT_LIMIT){
		return -1;
	}

	fdt[fd_num]=file;

	return fd_num;

}

struct file *get_file_by_fd(int fd) {
	struct thread *cur = thread_current();
	struct file **fdt = cur->fdt;

	if(fd>FDCOUNT_LIMIT || fd <2){
		return NULL;
	}

	return fdt[fd];
}

void delete_fd_from_fdt(int fd) {
	struct thread *cur = thread_current();
	struct file **fdt = cur->fdt;

	if(fd>FDCOUNT_LIMIT || fd <0){ //?fd가 0,1인 경우에는?
		return;
	}
	fdt[fd] = NULL;
}