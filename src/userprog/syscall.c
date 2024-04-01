#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/process.h"
#include "threads/vaddr.h"
#include "devices/shutdown.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "filesys/inode.h"
#include "lib/float.h"
struct lock file_lock;

void exit_process(void) {
      printf("%s: exit(-1)\n", thread_current()->pcb->process_name);
      process_exit(-1);
}

//检查传入的地址是否有效
void check_valid(uint32_t* p) {
  if(p == NULL || !is_user_vaddr(p) || pagedir_get_page(thread_current()->pcb->pagedir, p) == NULL) {
    exit_process();
  }
  return;
}

/*
  uint32_t*addr = p;
  while(len) {
    check_valid(addr);
    addr++;
    len--;
  }
  return;
*/

//传入指针和指针所指空间  比如argv是4byte，检查每个byte是否合法
void check_pointer(uint32_t*p, int length) {
  char *addr_b = (char *)p;
  while (length--)
    check_valid(addr_b++);
}

//比如argv[1]是char* 指向一段字符 检查每个字符是否有效 并且有没有'\0'
void check_string(char*p) {
  while(true) {
    check_valid((uint32_t*)p);
    if(*p == '\0')
      return;
    else p++;
  }
}

void check_argv(uint32_t*p, int num) {
  check_pointer(p, num * sizeof(uint32_t));
}

bool syscall_create(const char *file_name, unsigned init_size) {
  lock_acquire(&file_lock);
  bool ret = filesys_create(file_name, init_size);
  lock_release(&file_lock);
  return ret;
}

bool syscall_remove(const char* file_name) {
  lock_acquire(&file_lock);
  bool ret = filesys_remove(file_name);
  lock_release(&file_lock);
  return ret;
}

int syscall_open(const char* file_name) {
  lock_acquire(&file_lock);
  struct file* open_file = filesys_open(file_name);
  if(open_file == NULL) {
    lock_release(&file_lock);
    return -1;
  } else {
    int fd = process_openfile(open_file);
    lock_release(&file_lock);
    return fd;
  }
}

int syscall_filesize(int fd) {
  lock_acquire(&file_lock);
  struct file *file = find_file(fd);
  if(fd == NULL) {
    lock_release(&file_lock);
    return -1;
  }
  int size = file->inode->data.length;
  lock_release(&file_lock);
  return size;
}

int syscall_read(int fd, void* buffer, unsigned size) {
  lock_acquire(&file_lock);
  if(fd == STDIN_FILENO) {
    uint8_t * p = (uint8_t*) buffer;
    int read_size = 0;
    while(size--) {
      *p++ = input_getc();
      read_size++;
    }
  lock_release(&file_lock);
  return read_size;
  }

  struct file* file = find_file(fd);
  if(file == NULL) {
    lock_release(&file_lock);
    return -1;
  }
  int read_size = file_read(file, buffer, size);
  lock_release(&file_lock);
  return read_size;
}

int syscall_write(int fd, void* buffer, unsigned size) {
  lock_acquire(&file_lock);
  //可能需要break large
  if(fd == STDOUT_FILENO) {
      putbuf((char*)buffer, size);
      lock_release(&file_lock);
      return size;
  }
  struct file* file = find_file(fd);
  if(file == NULL) {
    lock_release(&file_lock);
    return -1;
  }
  int write_size = file_write(file, buffer, size);
  lock_release(&file_lock);
  return write_size;
}

void syscall_seek(int fd, off_t off) {
  lock_acquire(&file_lock);
  struct file* file = find_file(fd);
  if(file != NULL) {
    file_seek(file, off);
  }
  lock_release(&file_lock);
}

unsigned syscall_tell(int fd) {
  lock_acquire(&file_lock);
  struct file* file = find_file(fd);
  if(file == NULL) {
    lock_release(&file_lock);
    return -1;
  };
  unsigned po = file_tell(file);
  lock_release(&file_lock);
  return po;
}

void syscall_close(int fd) {
  lock_acquire(&file_lock);
  struct file* file = find_file(fd);
  if(file == NULL) {
    lock_release(&file_lock);
    return;
  };
  close_file(fd);
  file_close(file);
  lock_release(&file_lock);
}

static void syscall_handler(struct intr_frame*);

void syscall_init(void) { 
  intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall"); 
  lock_init(&file_lock);
  }

static void syscall_handler(struct intr_frame* f) {
  uint32_t* args = ((uint32_t*)f->esp);

  check_argv(args,1);
  /*
   * The following print statement, if uncommented, will print out the syscall
   * number whenever a process enters a system call. You might find it useful
   * when debugging. It will cause tests to fail, however, so you should not
   * include it in your final submission.
   */

  /* printf("System call number: %d\n", args[0]); */
  switch(args[0]) {
    case SYS_EXIT:
        check_argv(args+1, 1);
        printf("%s: exit(%d)\n", thread_current()->pcb->process_name, args[1]);
        process_exit(args[1]);
        break;
    case SYS_PRACTICE:
      check_argv(args+1, 1);
        f->eax = args[1] + 1;
        break;
    case SYS_HALT:
        shutdown_power_off();
        break;
    case SYS_EXEC:
        check_argv(args+1, 1);
        check_string((char*)args[1]);
        
        f->eax = process_execute((char*)args[1]);
        break;
    case SYS_WAIT:
        check_argv(args+1, 1);
        f->eax = process_wait(args[1]);
        break;
    case SYS_CREATE:
        check_argv(args+1, 2);
        check_string((char*)args[1]);
        f->eax = syscall_create((char*)args[1], args[2]);
        break;
    case SYS_REMOVE:
        check_argv(args+1, 1);
        check_string((char*)args[1]);
        f->eax = syscall_remove((char*)args[1]);
        break;
    case SYS_OPEN:
        check_argv(args+1, 1);
        check_string((char*)args[1]);
        f->eax = syscall_open((char*)args[1]);
        break;
    case SYS_FILESIZE:
        check_argv(args+1, 1);
        f->eax = syscall_filesize(args[1]);
        break;
    case SYS_READ:
        check_argv(args+1, 3);
        check_pointer((void*)args[2], args[3]);
        f->eax = syscall_read(args[1], (void*)args[2], (size_t)args[3]);
        break;
    case SYS_WRITE:
        check_argv(args+1, 3);
        check_pointer((void*)args[2], args[3]);
        f->eax = syscall_write(args[1], (void*)args[2], (size_t)args[3]);
        break;
    case SYS_SEEK:
        check_argv(args+1, 2);
        syscall_seek(args[1], args[2]);
        break;
    case SYS_TELL:
        check_argv(args+1, 1);
        f->eax = syscall_tell(args[1]);
        break;
    case SYS_CLOSE:
        check_argv(args+1, 1);
        syscall_close(args[1]);
        break;   
    case SYS_COMPUTE_E:
        check_argv(args+1, 1);
        f->eax = sys_sum_to_e(args[1]);
        break;
    case SYS_PT_CREATE:
        check_argv(args+1, 3);
        f->eax = pthread_execute((stub_fun)args[1], (pthread_fun)args[2], (void*)args[3]);
        break;
    case SYS_PT_JOIN:
        check_argv(args+1, 1);
        f->eax = pthread_join((tid_t)args[1]);
        break;
    case SYS_PT_EXIT:
        pthread_exit();
        break;
    default:
        NOT_REACHED();
        break;
  }

  
}
