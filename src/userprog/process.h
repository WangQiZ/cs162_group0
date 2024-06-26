#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include <stdint.h>
#include<list.h>
// At most 8MB can be allocated to the stack
// These defines will be used in Project 2: Multithreading
#define MAX_STACK_PAGES (1 << 11)
#define MAX_THREADS 127
#define MAX_ARGC 32
/* PIDs and TIDs are the same type. PID should be
   the TID of the main thread of the process */
typedef tid_t pid_t;

/* Thread functions (Project 2: Multithreading) */
typedef void (*pthread_fun)(void*);
typedef void (*stub_fun)(pthread_fun, void*);

/* The process control block for a given process. Since
   there can be multiple threads per process, we need a separate
   PCB from the TCB. All TCBs in a process will have a pointer
   to the PCB, and the PCB will have a pointer to the main thread
   of the process, which is `special`. */
struct process {
  /* Owned by process.c. */
  uint32_t* pagedir;          /* Page directory. */
  char process_name[16];      /* Name of the main thread */
  struct thread* main_thread; /* Pointer to main thread */
  pid_t father_pid;           //保存下父进程pid
  struct list file_list;
  struct file* exec_file;    //保存执行文件
  struct list pthread_list;  //保存进程下的线程
  struct list process_lock_list; //保存进程下所有的锁
  struct list process_sema_list;
  int pthread_count;
};

//子进程列表，需要保存返回状态，需要知道自己的pid，需要信号量来判断是否执行完成，需要知道父进程pid
struct child_process {
   struct list_elem child_elem;
   int exit_status;
   pid_t child_pid;
   pid_t father_pid;
   struct semaphore exit_wait;
};

//线程列表，和子进程类似
struct thread_process {
   struct list_elem pthread_elem;
   struct semaphore pthread_exit_wait;
   tid_t tid;
   bool has_joined;
   int pthread_exit_status;
};

//文件列表
struct process_file {
   struct list_elem file_elem;
   int fd;
   struct file* file;
};

struct user_lock {
   struct list_elem user_lock_elem;
   char lock_id;
   struct lock lock;
};

struct user_semaphore {
   struct list_elem user_sema_elem;
   char sema_id;
   struct semaphore sema;
};

int process_openfile(struct file* file);
struct file* find_file(int fd);
void close_file(int fd);
void userprog_init(void);

pid_t process_execute(const char* file_name);
int process_wait(pid_t);
void process_exit(int);
void process_activate(void);

bool is_main_thread(struct thread*, struct process*);
pid_t get_pid(struct process*);

tid_t pthread_execute(stub_fun, pthread_fun, void*);
tid_t pthread_join(tid_t);
void pthread_exit(void);
void pthread_exit_main(void);
bool init_user_lock(char* lock_id);
bool user_lock_acquire(char* lock_id);
bool user_lock_release(char* lock_id);
bool user_sema_init(char* sema_id, int value);
bool user_sema_down(char* sema_id);
bool user_sema_up(char* sema_id);
#endif /* userprog/process.h */
