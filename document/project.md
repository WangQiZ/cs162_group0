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
