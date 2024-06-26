#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "filesys/inode.h"
#include "userprog/syscall.h"

static thread_func start_process NO_RETURN;
static thread_func start_pthread NO_RETURN;
static bool load(const char* file_name, void (**eip)(void), void** esp);
bool setup_thread(void** esp, int num);


static struct list list_all_children;
static struct lock list_lock;
static struct lock fd_lock;
static struct lock pthread_lock;
static struct lock pthread_lock_lock;
static struct lock pthread_sema_lock;
static int file_descriptor = 5;

struct mul_args {
  char * process_cmd;
  pid_t father_pid;
  int is_success;
  struct semaphore child_create_sema;
  struct semaphore child_exec_sema;
};

/*关于文件的处理*/

int allocate_fd(void) {
  lock_acquire(&fd_lock);
  int fd = file_descriptor++;
  lock_release(&fd_lock);
  return fd;
}

int process_openfile(struct file* file) {
  struct process_file *pfile = (struct process_file*)malloc(sizeof(struct process_file));
  if(pfile == NULL)
  return -1;
  struct thread* t = thread_current();
  pfile->fd = allocate_fd();
  pfile->file = file;
  list_push_back(&t->pcb->file_list, &pfile->file_elem);
  return pfile->fd;
}

struct file* find_file(int fd) {
  struct thread *t = thread_current();
  struct list_elem *e;
  struct list *flist = &t->pcb->file_list;
  for(e = list_begin(flist); e != list_end(flist); e = list_next(e)) {
    struct process_file * pfile = list_entry(e, struct process_file, file_elem);
    if(pfile->fd == fd)
      return pfile->file;
  }
  return NULL;
}

void close_file(int fd) {
  struct thread *t = thread_current();
  struct list_elem *e;
  struct list *flist = &t->pcb->file_list;
  for(e = list_begin(flist); e != list_end(flist); e = list_next(e)) {
    struct process_file * pfile = list_entry(e, struct process_file, file_elem);
    if(pfile->fd == fd) {
      list_remove(&pfile->file_elem);
    }
      
  }
}

/* Initializes user programs in the system by ensuring the main
   thread has a minimal PCB so that it can execute and wait for
   the first user process. Any additions to the PCB should be also
   initialized here if main needs those members */
void userprog_init(void) {
  struct thread* t = thread_current();
  bool success;

  /* Allocate process control block
     It is imoprtant that this is a call to calloc and not malloc,
     so that t->pcb->pagedir is guaranteed to be NULL (the kernel's
     page directory) when t->pcb is assigned, because a timer interrupt
     can come at any time and activate our pagedir */
  t->pcb = calloc(sizeof(struct process), 1);
  success = t->pcb != NULL;

  list_init(&list_all_children);
  lock_init(&list_lock);
  lock_init(&fd_lock);
  lock_init(&pthread_lock);
  lock_init(&pthread_lock_lock);
  lock_init(&pthread_sema_lock);
  /* Kill the kernel if we did not succeed */
  ASSERT(success);
}

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   process id, or TID_ERROR if the thread cannot be created. */
pid_t process_execute(const char* proc_cmd) {
  char* fn_copy;
  tid_t tid;
  struct child_process *process_child;
  struct thread* t = thread_current();
  struct mul_args *process_args = (struct mul_args*)malloc(sizeof(struct mul_args));
  if(process_args == NULL)
    return TID_ERROR;
  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page(0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy(fn_copy, proc_cmd, PGSIZE);

  process_args->process_cmd = fn_copy;
  process_args->father_pid = t->tid;
  process_args->is_success = 0;
  sema_init(&process_args->child_create_sema, 0);
  sema_init(&process_args->child_exec_sema, 0);
  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create(proc_cmd, PRI_DEFAULT, start_process, (void*)process_args);


  sema_down(&process_args->child_create_sema);

  if(process_args->is_success == 0)
  {
    palloc_free_page(fn_copy);
    free(process_args);
    return TID_ERROR;
  };

  process_child = (struct child_process*)malloc(sizeof(struct child_process));
  if(process_child == NULL) {
    palloc_free_page(fn_copy);
    free(process_args);
    return TID_ERROR;
  };
  process_child->child_pid = tid;
  process_child->father_pid = t->tid;
  process_child->exit_status = 0;
  sema_init(&process_child->exit_wait, 0);
  list_push_back(&list_all_children, &process_child->child_elem);

  sema_up(&process_args->child_exec_sema);

  palloc_free_page(fn_copy);

  return tid;
}

/* A thread function that loads a user process and starts it
   running. */
static void start_process(void* args_) {
  struct mul_args* process_args = (struct mul_args*)args_;
  struct thread* t = thread_current();
  struct intr_frame if_;
  bool success, pcb_success;

  int argc = 0;
  char *argv[MAX_ARGC];
  int cmd_total_len = 0;
  /* Allocate process control block */
  struct process* new_pcb = malloc(sizeof(struct process));
  success = pcb_success = new_pcb != NULL;

  char *cmd_ = process_args->process_cmd;
  /*拆分字符串*/
  if(success) {
    char *saveptr;
    char *token = strtok_r(cmd_, " ", &saveptr);

    for(argc = 0; argc < MAX_ARGC && token != NULL; argc++) {
      int arg_len = strlen(token) + 1;
      argv[argc] = (char*)malloc(arg_len);
      strlcpy(argv[argc], token, arg_len);
      cmd_total_len += arg_len;
      token = strtok_r(NULL, " ", &saveptr);
    }

  }
  /* Initialize process control block */
  if (success) {
    // Ensure that timer_interrupt() -> schedule() -> process_activate()
    // does not try to activate our uninitialized pagedir
    new_pcb->pagedir = NULL;
    t->pcb = new_pcb;

    // Continue initializing the PCB as normal
    t->pcb->main_thread = t;
    t->pcb->father_pid = process_args->father_pid;
    strlcpy(t->pcb->process_name, argv[0], (strlen(argv[0]) + 1));
    list_init(&t->pcb->file_list);
    list_init(&t->pcb->pthread_list);
    list_init(&t->pcb->process_lock_list);
    list_init(&t->pcb->process_sema_list);
    t->pcb->pthread_count = 0;
  }

  /* Initialize interrupt frame and load executable. */
  if (success) {
    memset(&if_, 0, sizeof if_);
    if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
    if_.cs = SEL_UCSEG;
    if_.eflags = FLAG_IF | FLAG_MBS;
    success = load(argv[0], &if_.eip, &if_.esp);
  }

  //下面要进行对齐 然后把参数压入stack
  if(success) {
    // argv[] + argc + argv + data
    int size = (argc + 1) * sizeof(char *) + sizeof(int) + sizeof(char**) + cmd_total_len;
    // 对齐 需要额外
    int align_size = 0x10 - size % 0x10;
    if_.esp -= (size + align_size);
    memset(if_.esp, 0, size + align_size);

    void* esp = if_.esp + size + align_size;
    
    // esp -= cmd_total_len;
    // memcpy(esp, cmd, cmd_total_len);
    for(int i = argc - 1; i >=0; i--) {
      esp -= (strlen(argv[i]) + 1);
      memcpy(esp, argv[i], (strlen(argv[i]) + 1));
    }


    void* position = esp;

    esp -= align_size;
    esp -= 0x4;
    *(char**)esp = NULL; 
    int offset = cmd_total_len;
    // push pointers to the arguments onto the stack
    for (int i = argc - 1; i >= 0; i--) {
      esp -= 0x4;
      offset -= (strlen(argv[i]) + 1);
      *(char**)esp = position + offset;
    }
    
    esp -= 0x4;
    *(char***)esp = (esp + 0x4); 

    esp -= 0x4;
    *(int*)esp = argc; 

    esp -= 0x4;
    *(char**)esp = NULL; 
    if_.esp = esp;
  }

  /* Handle failure with succesful PCB malloc. Must free the PCB */
  if (!success && pcb_success) {
    // Avoid race where PCB is freed before t->pcb is set to NULL
    // If this happens, then an unfortuantely timed timer interrupt
    // can try to activate the pagedir, but it is now freed memory
    struct process* pcb_to_free = t->pcb;
    t->pcb = NULL;
    free(pcb_to_free);
  }
  process_args->is_success = success;
  sema_up(&process_args->child_create_sema);
  sema_down(&process_args->child_exec_sema);
  free(process_args);
  /* Clean up. Exit on failure or jump to userspace */
  if (!success) {
    thread_exit();
  }
  asm("fsave (%0);" : : "g"(&if_.fpu_register));
  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile("movl %0, %%esp; jmp intr_exit" : : "g"(&if_) : "memory");
  NOT_REACHED();
}

//
struct child_process* find_child(pid_t father_pid, pid_t child_pid) {
      lock_acquire(&list_lock);
    struct list_elem *e;
    for(e = list_begin(&list_all_children); e != list_end(&list_all_children); e = list_next(e)) {
      struct child_process *child = list_entry(e, struct child_process, child_elem);
      if(child->father_pid == father_pid && child->child_pid == child_pid) {
            lock_release(&list_lock);
        return child;
      }
    }
        lock_release(&list_lock);
    return NULL;
}

/* Waits for process with PID child_pid to die and returns its exit status.
   If it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If child_pid is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given PID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int process_wait(pid_t child_pid) {

    struct thread *t = thread_current();
    
    struct child_process *child = find_child(t->tid, child_pid);
    if(child == NULL) {
      return -1;
    }
    sema_down(&child->exit_wait);
    int exit_status = child->exit_status;
    list_remove(&child->child_elem);
    free(child);
  return exit_status;
}

void kill_all_child(pid_t father_pid) {
  lock_acquire(&list_lock);

  struct list_elem * e;
  for(e = list_begin(&list_all_children); e != list_end(&list_all_children); e = list_next(e)) {
    struct child_process *child = list_entry(e, struct child_process, child_elem);
    if(child->father_pid == father_pid) {
      list_remove(&child->child_elem);
      free(child);
    }
  }
  lock_release(&list_lock);
}

void close_all_file(struct process *p) {
  struct list_elem *e, *next;
  for(e = list_begin(&p->file_list); e != list_end(&p->file_list); e = next) {
    next = list_next(e);
    struct process_file *pro_file = list_entry(e, struct process_file, file_elem);
    list_remove(&pro_file->file_elem);
    file_close(pro_file->file);
    free(pro_file);
  }
}

/* Free the current process's resources. */
void process_exit(int status) {
  struct thread* cur = thread_current();
  uint32_t* pd;

  /* If this thread does not have a PCB, don't worry */
  if (cur->pcb == NULL) {
    thread_exit();
    NOT_REACHED();
  }

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pcb->pagedir;
  if (pd != NULL) {
    /* Correct ordering here is crucial.  We must set
         cur->pcb->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
    cur->pcb->pagedir = NULL;
    pagedir_activate(NULL);
    pagedir_destroy(pd);
  }

    struct child_process * child = find_child(cur->pcb->father_pid, cur->tid);
    if(child) {
      child->exit_status = status;
      sema_up(&child->exit_wait);
    }

    //杀死所有子进程
    kill_all_child(cur->tid);
    file_close(cur->pcb->exec_file);
    close_all_file(cur->pcb);
    

  /* Free the PCB of this process and kill this thread
     Avoid race where PCB is freed before t->pcb is set to NULL
     If this happens, then an unfortuantely timed timer interrupt
     can try to activate the pagedir, but it is now freed memory */
  struct process* pcb_to_free = cur->pcb;
  cur->pcb = NULL;
  free(pcb_to_free);

  thread_exit();
}

/* Sets up the CPU for running user code in the current
   thread. This function is called on every context switch. */
void process_activate(void) {
  struct thread* t = thread_current();

  /* Activate thread's page tables. */
  if (t->pcb != NULL && t->pcb->pagedir != NULL)
    pagedir_activate(t->pcb->pagedir);
  else
    pagedir_activate(NULL);

  /* Set thread's kernel stack for use in processing interrupts.
     This does nothing if this is not a user process. */
  tss_update();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32 /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32 /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32 /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16 /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr {
  unsigned char e_ident[16];
  Elf32_Half e_type;
  Elf32_Half e_machine;
  Elf32_Word e_version;
  Elf32_Addr e_entry;
  Elf32_Off e_phoff;
  Elf32_Off e_shoff;
  Elf32_Word e_flags;
  Elf32_Half e_ehsize;
  Elf32_Half e_phentsize;
  Elf32_Half e_phnum;
  Elf32_Half e_shentsize;
  Elf32_Half e_shnum;
  Elf32_Half e_shstrndx;
};

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr {
  Elf32_Word p_type;
  Elf32_Off p_offset;
  Elf32_Addr p_vaddr;
  Elf32_Addr p_paddr;
  Elf32_Word p_filesz;
  Elf32_Word p_memsz;
  Elf32_Word p_flags;
  Elf32_Word p_align;
};

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL 0           /* Ignore. */
#define PT_LOAD 1           /* Loadable segment. */
#define PT_DYNAMIC 2        /* Dynamic linking info. */
#define PT_INTERP 3         /* Name of dynamic loader. */
#define PT_NOTE 4           /* Auxiliary info. */
#define PT_SHLIB 5          /* Reserved. */
#define PT_PHDR 6           /* Program header table. */
#define PT_STACK 0x6474e551 /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1 /* Executable. */
#define PF_W 2 /* Writable. */
#define PF_R 4 /* Readable. */

static bool setup_stack(void** esp);
static bool validate_segment(const struct Elf32_Phdr*, struct file*);
static bool load_segment(struct file* file, off_t ofs, uint8_t* upage, uint32_t read_bytes,
                         uint32_t zero_bytes, bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool load(const char* file_name, void (**eip)(void), void** esp) {
  lock_acquire(&file_lock);
  struct thread* t = thread_current();
  struct Elf32_Ehdr ehdr;
  struct file* file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pcb->pagedir = pagedir_create();
  if (t->pcb->pagedir == NULL)
    goto done;
  process_activate();

  /* Open executable file. */
  file = filesys_open(file_name);
  if (file == NULL) {
    printf("load: %s: open failed\n", file_name);
    goto done;
  }
  file_deny_write(file);
  /* Read and verify executable header. */
  if (file_read(file, &ehdr, sizeof ehdr) != sizeof ehdr ||
      memcmp(ehdr.e_ident, "\177ELF\1\1\1", 7) || ehdr.e_type != 2 || ehdr.e_machine != 3 ||
      ehdr.e_version != 1 || ehdr.e_phentsize != sizeof(struct Elf32_Phdr) || ehdr.e_phnum > 1024) {
    printf("load: %s: error loading executable\n", file_name);
    goto done;
  }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) {
    struct Elf32_Phdr phdr;

    if (file_ofs < 0 || file_ofs > file_length(file))
      goto done;
    file_seek(file, file_ofs);

    if (file_read(file, &phdr, sizeof phdr) != sizeof phdr)
      goto done;
    file_ofs += sizeof phdr;
    switch (phdr.p_type) {
      case PT_NULL:
      case PT_NOTE:
      case PT_PHDR:
      case PT_STACK:
      default:
        /* Ignore this segment. */
        break;
      case PT_DYNAMIC:
      case PT_INTERP:
      case PT_SHLIB:
        goto done;
      case PT_LOAD:
        if (validate_segment(&phdr, file)) {
          bool writable = (phdr.p_flags & PF_W) != 0;
          uint32_t file_page = phdr.p_offset & ~PGMASK;
          uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
          uint32_t page_offset = phdr.p_vaddr & PGMASK;
          uint32_t read_bytes, zero_bytes;
          if (phdr.p_filesz > 0) {
            /* Normal segment.
                     Read initial part from disk and zero the rest. */
            read_bytes = page_offset + phdr.p_filesz;
            zero_bytes = (ROUND_UP(page_offset + phdr.p_memsz, PGSIZE) - read_bytes);
          } else {
            /* Entirely zero.
                     Don't read anything from disk. */
            read_bytes = 0;
            zero_bytes = ROUND_UP(page_offset + phdr.p_memsz, PGSIZE);
          }
          if (!load_segment(file, file_page, (void*)mem_page, read_bytes, zero_bytes, writable))
            goto done;
        } else
          goto done;
        break;
    }
  }

  /* Set up stack. */
  if (!setup_stack(esp))
    goto done;

  /* Start address. */
  *eip = (void (*)(void))ehdr.e_entry;

  success = true;

done:
  /* We arrive here whether the load is successful or not. */
  if(success) {
    t->pcb->exec_file = file;
  } else {
    file_close(file);
  }
  lock_release(&file_lock);
  return success;
}

/* load() helpers. */

static bool install_page(void* upage, void* kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool validate_segment(const struct Elf32_Phdr* phdr, struct file* file) {
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
    return false;

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off)file_length(file))
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz)
    return false;

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;

  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr((void*)phdr->p_vaddr))
    return false;
  if (!is_user_vaddr((void*)(phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool load_segment(struct file* file, off_t ofs, uint8_t* upage, uint32_t read_bytes,
                         uint32_t zero_bytes, bool writable) {
  ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT(pg_ofs(upage) == 0);
  ASSERT(ofs % PGSIZE == 0);

  file_seek(file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) {
    /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
    size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
    size_t page_zero_bytes = PGSIZE - page_read_bytes;

    /* Get a page of memory. */
    uint8_t* kpage = palloc_get_page(PAL_USER);
    if (kpage == NULL)
      return false;

    /* Load this page. */
    if (file_read(file, kpage, page_read_bytes) != (int)page_read_bytes) {
      palloc_free_page(kpage);
      return false;
    }
    memset(kpage + page_read_bytes, 0, page_zero_bytes);

    /* Add the page to the process's address space. */
    if (!install_page(upage, kpage, writable)) {
      palloc_free_page(kpage);
      return false;
    }

    /* Advance. */
    read_bytes -= page_read_bytes;
    zero_bytes -= page_zero_bytes;
    upage += PGSIZE;
  }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool setup_stack(void** esp) {
  uint8_t* kpage;
  bool success = false;

  kpage = palloc_get_page(PAL_USER | PAL_ZERO);
  if (kpage != NULL) {
    success = install_page(((uint8_t*)PHYS_BASE) - PGSIZE, kpage, true);
    if (success)
      *esp = PHYS_BASE;
    else
      palloc_free_page(kpage);
  }
  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool install_page(void* upage, void* kpage, bool writable) {
  struct thread* t = thread_current();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page(t->pcb->pagedir, upage) == NULL &&
          pagedir_set_page(t->pcb->pagedir, upage, kpage, writable));
}

/* Returns true if t is the main thread of the process p */
bool is_main_thread(struct thread* t, struct process* p) { return p->main_thread == t; }

/* Gets the PID of a process */
pid_t get_pid(struct process* p) { return (pid_t)p->main_thread->tid; }

/* Creates a new stack for the thread and sets up its arguments.
   Stores the thread's entry point into *EIP and its initial stack
   pointer into *ESP. Handles all cleanup if unsuccessful. Returns
   true if successful, false otherwise.

   This function will be implemented in Project 2: Multithreading. For
   now, it does nothing. You may find it necessary to change the
   function signature. */
bool setup_thread(void** esp, int num) {
  uint8_t* kpage;
  bool success = false;

  kpage = palloc_get_page(PAL_USER | PAL_ZERO);
  if (kpage != NULL) {
    uint8_t* base = (uint8_t*)PHYS_BASE - (PGSIZE * 2 * num);
    success = install_page(base - PGSIZE, kpage, true); //目前就分配一个thread的stack
    if (success)
      *esp = base;
    else
      palloc_free_page(kpage);
  }
  return success;
}

struct pthread_args {
  stub_fun sf;
  pthread_fun tf;
  void* arg;
  struct process * pcb;
  bool is_setup;
  struct semaphore pthread_setup_sema;
};
/* Starts a new thread with a new user stack running SF, which takes
   TF and ARG as arguments on its user stack. This new thread may be
   scheduled (and may even exit) before pthread_execute () returns.
   Returns the new thread's TID or TID_ERROR if the thread cannot
   be created properly.

   This function will be implemented in Project 2: Multithreading and
   should be similar to process_execute (). For now, it does nothing.
   */
tid_t pthread_execute(stub_fun sf, pthread_fun tf, void* arg) {
  struct thread * cur = thread_current();
  tid_t tid;
  struct pthread_args * t_args = (struct pthread_args*)malloc(sizeof(struct pthread_args));
  if(t_args == NULL)
    return TID_ERROR;
  
  t_args->sf = sf;
  t_args->tf = tf;
  t_args->arg = arg;
  t_args->pcb = cur->pcb;
  t_args->is_setup = false;
  sema_init(&t_args->pthread_setup_sema, 0);

  tid = thread_create(cur->name, PRI_DEFAULT, start_pthread, (void*)t_args);

  if(tid == TID_ERROR) {
    free(t_args);
    return tid;
  }

  sema_down(&t_args->pthread_setup_sema);

  if(t_args->is_setup == false) {
    free(t_args);
    return TID_ERROR;
  }

  free(t_args);
  return tid;
}

/* A thread function that creates a new user thread and starts it
   running. Responsible for adding itself to the list of threads in
   the PCB.

   This function will be implemented in Project 2: Multithreading and
   should be similar to start_process (). For now, it does nothing. */
static void start_pthread(void* args_) {
  struct pthread_args * t_args = (struct pthread_args*) args_;
  struct thread* t = thread_current();
  struct intr_frame if_;
  bool success;

  struct thread_process *pthread_process = (struct thread_process*)malloc(sizeof(struct thread_process));
  if(pthread_process == NULL) {
    t_args->is_setup = false;
    sema_up(&t_args->pthread_setup_sema);
    thread_exit();
  }

  t->pcb = t_args->pcb;
  process_activate();

  //初始化if_
  memset(&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  if_.eip = (void*)t_args->sf;

  int num = 0;
  lock_acquire(&pthread_lock);
  num = ++t->pcb->pthread_count;
  lock_release(&pthread_lock);
  
  //只能创建一个thread版本
  success = setup_thread(&if_.esp, num);

  if(!success) {
    t_args->is_setup = false;
    sema_up(&t_args->pthread_setup_sema);
    thread_exit();
  }

  /*
    设置esp
esp->
     |      align                   |
     |      argv[1]=(void*)arg      |
     |      argv[0]=tf              |
     |      fake return             | 
  */

  void *esp = if_.esp;
  //align
  esp -= 4;
  *(void**)esp = NULL;
  //argv[1]
  esp -= 4;
  *(void**)esp = t_args->arg;
  //argv[0]
  esp -= 4;
  *(void**)esp = t_args->tf;
  //fake return
  esp -= 4;
  *(void**)esp = NULL;
  if_.esp = esp;

 //初始化pthread_process
  pthread_process->tid = t->tid;
  pthread_process->pthread_exit_status = false;
  pthread_process->has_joined = false;
  sema_init(&pthread_process->pthread_exit_wait, 0);
//插入到list,需要加锁
  lock_acquire(&pthread_lock);
  list_push_back(&t_args->pcb->pthread_list, &pthread_process->pthread_elem);
  lock_release(&pthread_lock);

  t_args->is_setup = true;
  sema_up(&t_args->pthread_setup_sema);
  asm("fsave (%0);" : : "g"(&if_.fpu_register));  
  asm volatile("movl %0, %%esp; jmp intr_exit" : : "g"(&if_) : "memory");
}

struct thread_process* find_pthread(tid_t tid, struct process* pcb){
    struct list_elem *e;
    struct list pthread_list = pcb->pthread_list;
    for(e = list_begin(&pthread_list); e != list_end(&pthread_list); e = list_next(e)) {
      struct thread_process * pthread_process = list_entry(e, struct thread_process, pthread_elem);
      if(pthread_process->tid == tid)
      return pthread_process;
    }
    return NULL;
}

/* Waits for thread with TID to die, if that thread was spawned
   in the same process and has not been waited on yet. Returns TID on
   success and returns TID_ERROR on failure immediately, without
   waiting.

   This function will be implemented in Project 2: Multithreading. For
   now, it does nothing. */
tid_t pthread_join(tid_t tid) {
  struct thread *t = thread_current();
  if(t->tid == tid)
    return TID_ERROR;

  lock_acquire(&pthread_lock);
  struct thread_process* pthread_process = find_pthread(tid, t->pcb);

  if(pthread_process == NULL || pthread_process->has_joined == true) {
    lock_release(&pthread_lock);
    return TID_ERROR;
  }

  pthread_process->has_joined = true;

  if(pthread_process->pthread_exit_status) {
    lock_release(&pthread_lock);
    return tid;
  }
  lock_release(&pthread_lock);
  sema_down(&pthread_process->pthread_exit_wait);
  return tid;
}

/* Free the current thread's resources. Most resources will
   be freed on thread_exit(), so all we have to do is deallocate the
   thread's userspace stack. Wake any waiters on this thread.

   The main thread should not use this function. See
   pthread_exit_main() below.

   This function will be implemented in Project 2: Multithreading. For
   now, it does nothing. */
void pthread_exit(void) {
  struct thread* t = thread_current();
  lock_acquire(&pthread_lock);
  struct thread_process* pthread_process = find_pthread(t->tid, t->pcb);

  sema_up(&pthread_process->pthread_exit_wait);
  lock_release(&pthread_lock);
  thread_exit();
}

/* Only to be used when the main thread explicitly calls pthread_exit.
   The main thread should wait on all threads in the process to
   terminate properly, before exiting itself. When it exits itself, it
   must terminate the process in addition to all necessary duties in
   pthread_exit.

   This function will be implemented in Project 2: Multithreading. For
   now, it does nothing. */
void pthread_exit_main(void) {}


//实际上传的是地址
bool init_user_lock(char* lock_id) {
  if(lock_id == NULL)
    return false;
  struct thread* t = thread_current();
  struct user_lock* pthread_user_lock = malloc(sizeof(struct user_lock));
  if(pthread_user_lock == NULL)
    return false;
  pthread_user_lock->lock_id = *lock_id;
  lock_init(&pthread_user_lock->lock);
  lock_acquire(&pthread_lock_lock);
  list_push_back(&t->pcb->process_lock_list, &pthread_user_lock->user_lock_elem);
  lock_release(&pthread_lock_lock);

  return true;
}

bool user_lock_acquire(char* lock_id) {
  if(lock_id == NULL)
    return false;
  struct thread* t = thread_current();
  struct user_lock* pthread_user_lock = NULL;
  lock_acquire(&pthread_lock_lock);
  struct list_elem *e;
  for(e = list_begin(&t->pcb->process_lock_list); e != list_end(&t->pcb->process_lock_list);
  e = list_next(e)) {
    struct user_lock* tmp = list_entry(e, struct user_lock, user_lock_elem);
    if(tmp->lock_id == *lock_id){
      pthread_user_lock = tmp;
      break;
    }
  }
  lock_release(&pthread_lock_lock);
  if(pthread_user_lock == NULL || pthread_user_lock->lock.holder == t)
    return false;

  lock_acquire(&pthread_user_lock->lock);
  return true;
}

bool user_lock_release(char* lock_id) {
  if(lock_id == NULL)
    return false;
  struct thread* t = thread_current();
  struct user_lock* pthread_user_lock = NULL;
  lock_acquire(&pthread_lock_lock);
  struct list_elem *e;
  for(e = list_begin(&t->pcb->process_lock_list); e != list_end(&t->pcb->process_lock_list);
  e = list_next(e)) {
    struct user_lock* tmp = list_entry(e, struct user_lock, user_lock_elem);
    if(tmp->lock_id == *lock_id){
      pthread_user_lock = tmp;
      break;
    }
  }
  lock_release(&pthread_lock_lock);
  if(pthread_user_lock == NULL || pthread_user_lock->lock.holder != t)
    return false; 
  lock_release(&pthread_user_lock->lock);
  return true;
}

bool user_sema_init(char* sema_id, int value) {
  if(sema_id == NULL || value < 0)
    return false;
  struct thread *t = thread_current();
  struct user_semaphore *user_sema = malloc(sizeof(struct user_semaphore));
  if(user_sema == NULL)
    return false;
  user_sema->sema_id = *sema_id;
  sema_init(&user_sema->sema, value);
  lock_acquire(&pthread_sema_lock);
  list_push_back(&t->pcb->process_sema_list, &user_sema->user_sema_elem);
  lock_release(&pthread_sema_lock);

  return true;  
}

bool user_sema_down(char* sema_id) {
  if(sema_id == NULL)
    return false;
  struct thread* t = thread_current();
  struct user_semaphore * user_sema = NULL;
  lock_acquire(&pthread_sema_lock);
  struct list_elem *e;
  for(e = list_begin(&t->pcb->process_sema_list); e != list_end(&t->pcb->process_sema_list);
  e = list_next(e)) {
    struct user_semaphore *tmp = list_entry(e, struct user_semaphore, user_sema_elem);
    if(tmp->sema_id == sema_id) {
      user_sema == tmp;
      break;
    }
  }
  lock_release(&pthread_sema_lock);
  if(user_sema == NULL)
    return false;
  sema_down(&user_sema->sema);
  return true;
}

bool user_sema_up(char* sema_id) {
  if(sema_id == NULL)
    return false;
  struct thread* t = thread_current();
  struct user_semaphore * user_sema = NULL;
  lock_acquire(&pthread_sema_lock);
  struct list_elem *e;
  for(e = list_begin(&t->pcb->process_sema_list); e != list_end(&t->pcb->process_sema_list);
  e = list_next(e)) {
    struct user_semaphore *tmp = list_entry(e, struct user_semaphore, user_sema_elem);
    if(tmp->sema_id == sema_id) {
      user_sema == tmp;
      break;
    }
  }
  lock_release(&pthread_sema_lock);
  if(user_sema == NULL)
    return false;
  sema_up(&user_sema->sema);
  return true;
}