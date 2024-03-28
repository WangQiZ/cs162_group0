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

优先级
1.改thread_create 比较现在的线程和新插入的线程的优先级，及时yield
2.thread_unblock 线程应该按照优先级进入队列
3.thread_yield 当前线程应该放弃cpu并且按照优先级进入队列
4.thread_set_priority 设置当前线程优先级 对ready_list重新排序

改变同步源语
lock semaphore condition variables

1.修改使之以优先级的顺序插入waiter list
sema_down cond_wait
2.按照优先级排序
在waiters list的线程修改优先级
sema_up 
cond_signal

优先级反转
优先级捐赠
nested donation
multiple donation
维护捐赠者列表
修改
init_thread -> 初始化捐赠的数据结构
lock_acquire 
1.如果lock不可用，保存锁的地址
2.保存现在的优先级，维护在list
3.捐赠

lock_release
移除线程恢复优先级
thread_set_priority
根据捐赠设置优先级

1.  在一个线程获取一个锁的时候， 如果拥有这个锁的线程优先级比自己低就提高它的优先级，并且如果这个锁还被别的锁锁着， 将会递归地捐赠优先级， 然后在这个线程释放掉这个锁之后恢复未捐赠逻辑下的优先级。

2. 如果一个线程被多个线程捐赠， 维持当前优先级为捐赠优先级中的最大值（acquire和release之时）。

3. 在对一个线程进行优先级设置的时候， 如果这个线程处于被捐赠状态， 则对original_priority进行设置， 然后如果设置的优先级大于当前优先级， 则改变当前优先级， 否则在捐赠状态取消的时候恢复original_priority。

4. 在释放锁对一个锁优先级有改变的时候应考虑其余被捐赠优先级和当前优先级。

5. 将信号量的等待队列实现为优先级队列。

6. 将condition的waiters队列实现为优先级队列。

7. 释放锁的时候若优先级改变则可以发生抢占。

thread应该保存base priority， 记录拥有的lock list 记录需要占用的lock
lock需要记录下当前多少thread想要占用 记录下目前最高的donation
