# Argument Passing

    process_execute 创建一个名为proc_cmd的thread，default优先级，运行start_process，
现在要修改start_process，将传入的cmd分解成argv，修改esp来传入参数

对齐和参数入栈
|    fake return     |
|    data            |
|    stack-align     |
|    argv[argc]      |
|    argv[0]         |
|    argv            |     char**
|    argc            |     int 
|    fake return     | <-- esp


# Process Control Syscalls

syscall需要知道user stack中的参数，为了保证内核安全需要检查传入syscall的stack pointer

1. null pointers, invalid pointers (e.g. pointing to unmapped memory), and illegal pointers (e.g. pointing to kernel memory). 
2. Beware: a 4-byte memory region (e.g. a 32-bit integer) may consist of 2 bytes of valid memory and 2 bytes of invalid memory, if the memory lies on a page boundary. You should handle these cases by terminating the user process.

static void start_process(void* cmd_) {
  char* cmd = (char*)cmd_;
  struct thread* t = thread_current();
  struct intr_frame if_;
  bool success, pcb_success;

  int argc = 0;
  char *argv[MAX_ARGC];
  int cmd_total_len = 0;
  /* Allocate process control block */
  struct process* new_pcb = malloc(sizeof(struct process));
  success = pcb_success = new_pcb != NULL;

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
    strlcpy(t->pcb->process_name, argv[0], (strlen(argv[0]) + 1));
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

  /* Clean up. Exit on failure or jump to userspace */
  palloc_free_page(cmd_);
  if (!success) {
    sema_up(&temporary);
    thread_exit();
  }

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile("movl %0, %%esp; jmp intr_exit" : : "g"(&if_) : "memory");
  NOT_REACHED();
}