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


wait
1. 需要根据pid找到child_process，需要知道child_process有没有执行完毕 如果执行完需要知道child_process的返回状态


暂时的思路：
1. process init的时候创建一个child_process list
2. process excute中在子进程创建后要更新list
3. wait遍历list根据状态返回

注意process的order
原order
父进程创建 父进程等待 子进程准备 子进程运行 

打印一些语句发现, 去掉semaphore temporary之后 父进程未等到子进程准备好就退出


检查地址有效用了一种很慢的方法----检查每一个地址