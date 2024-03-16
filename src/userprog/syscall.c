#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/process.h"
#include "threads/vaddr.h"
#include "devices/shutdown.h"

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
//传入指针和指针所指空间  比如argv是4byte，检查每个byte是否合法
void check_pointer(uint32_t*p, int len) {
  uint32_t*addr = p;
  while(len) {
    check_valid(addr);
    addr++;
    len--;
  }
  return;
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

static void syscall_handler(struct intr_frame*);

void syscall_init(void) { intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall"); }

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
    case SYS_WRITE:
        if(args[1] == STDOUT_FILENO) {
          putbuf((char*)args[2], args[3]);
          f->eax = args[3];
        } else {
          NOT_REACHED();
        }
        break;
    default:
        NOT_REACHED();
        break;
  }

  
}
