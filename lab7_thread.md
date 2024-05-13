# lab7.1 Uthread(moderate)

## 实验内容

仿造进程切换，实现用户态协程切换机制，完善线程创建及线程调度相关代码

该实验只是代码控制的线程切换，不涉及CPU的调度

## 实验流程

1. 仿造内核进程创建，在线程创建时需要设置用户线程上下文，因此需定义线程上下文结构体thread_context，参考内核进程上下文结构体，主要包括ra，sp等寄存器，并在线程结构体内添加线程上下文成员变量

   ```
   //roster new add
   struct thread_context {
     uint64 ra;
     uint64 sp;

     uint64 s0;
     uint64 s1;
     uint64 s2;
     uint64 s3;
     uint64 s4;
     uint64 s5;
     uint64 s6;
     uint64 s7;
     uint64 s8;
     uint64 s9;
     uint64 s10;
     uint64 s11;
   };

   struct thread {
     char       stack[STACK_SIZE]; /* the thread's stack */
     int        state;             /* FREE, RUNNING, RUNNABLE */

     //roster new add
     struct thread_context context;  //thread's context
   };
   ```
2. 完善线程创建代码，添加上下文的设置

   ```
   void 
   thread_create(void (*func)())
   {
     struct thread *t;

     for (t = all_thread; t < all_thread + MAX_THREAD; t++) {
       if (t->state == FREE) break;
     }
     t->state = RUNNABLE;
     // YOUR CODE HERE
     //参考allocproc创建新进程，设置线程上下文返回地址ra，栈顶指针sp
     t->context.ra = (uint64)func;     //将传入的函数指针作为线程的返回地址
     t->context.sp = (uint64)t->stack + (STACK_SIZE - 1); //设置栈顶指针，由于栈是向下生长，故栈顶指针设置最高地址

   }
   ```

   ra为传入的线程函数的地址，这样第一次调用该线程时，thread_switch函数内ret返回到ra地址（也就是线程函数地址），这样就能跳转到线程函数内执行（注：后续调用该线程时，ra地址又yiled调用的地址决斗，不再是线程函数指针的地址）；sp为线程的栈顶指针，每个线程都需要有一个独立的线程栈
3. 在uthread_switch.S添加线程切换函数的汇编代码

   ```
   thread_switch:
   	/* YOUR CODE HERE */
   	sd ra, 0(a0)
       sd sp, 8(a0)
       sd s0, 16(a0)
       sd s1, 24(a0)
       sd s2, 32(a0)
       sd s3, 40(a0)
       sd s4, 48(a0)
       sd s5, 56(a0)
       sd s6, 64(a0)
       sd s7, 72(a0)
       sd s8, 80(a0)
       sd s9, 88(a0)
       sd s10, 96(a0)
       sd s11, 104(a0)

       ld ra, 0(a1)
       ld sp, 8(a1)
       ld s0, 16(a1)
       ld s1, 24(a1)
       ld s2, 32(a1)
       ld s3, 40(a1)
       ld s4, 48(a1)
       ld s5, 56(a1)
       ld s6, 64(a1)
       ld s7, 72(a1)
       ld s8, 80(a1)
       ld s9, 88(a1)
       ld s10, 96(a1)
       ld s11, 104(a1)

       ret    /* return to ra */
   ```

   该函数的调用示例 `thread_switch(&current->context,  &next->context)`，其中函数上半部分就是保存current线程上下文信息，用于下次恢复线程，下半部分是加载next线程的上下文信息，其中ra就是上次保存的返回地址，接着ret返回到ra地址，若不是第一次调用，ra地址就是next线程上次调用thread_switch函数的地址，若是第一次调用thread_switch函数，ra为事先设置好的地址，也就是线程函数的地址
4. 在thread_schedule函数内补全线程切换的代码，也就是调用thread_switch函数

   ```
   /* YOUR CODE HERE
        * Invoke thread_switch to switch from t to next_thread:
        * thread_switch(??, ??);
        */
       thread_switch((uint64)&(t->context), (uint64)&(next_thread->context));
   ```

   ## 整个测试程序如何运行可以看uthread内的注释

## 关于CPU的调度详细请看这篇博文

[6.S081——CPU调度部分(CPU的复用和调度)——xv6源码完全解析系列(10)\_分析总结xv6中进程调度算法的实现-CSDN博客

](https://blog.csdn.net/zzy980511/article/details/131519246)

# lab7.2 Using Threads(moderate)

## 实验内容

多线程访问同一哈希表，使用锁解决数据丢失的问题，该实验与xv6没有关系，用的是自己的操作系统

## 实验流程

1. 首先明白为何会造车数据丢失，当两个线程同时进行put操作时，若线程A和线程B同时插入新的key，其中线程A插入可key1，线程B插入key2，且他们都进入到insert函数，线程A将key1插入到头部，但还没有执行`*p = e`,即此时头部还未更新到key1，而线程B将key2插入头部，此时就会覆盖到key1，造成key1丢失。因此造成key丢失的原因就是插入操作不具备原子性，需要加锁
2. 首先定义锁，在main函数初始化锁，在put函数内，在函数首尾加锁，解锁，注意要在查询key之前加锁，否则可能会造成插入重复key的行为。运行后发现没有 0 key missing，但是多线程put速率降低，甚至低于单线程，这个因为由于锁的原因，put函数只能单线程执行，且由于锁的时间开销，进一步降低了效率。
3. 现在考虑提高速率，程序中哈希表使用到了5个bucket，因此只要多线程不同时访问同一bucket，就不会出现race-condition（竞态条件），因此只要给5个bucket各加一把锁即可，这样，只有多线程正好需要访问同一bucket时才会单线程执行，在main函数内初始化5个锁，在put函数内访问哪个bucket，就为哪个bucket加锁。运行后解决效率问题
4. 由于get函数只会遍历bucket，不会更改哈希表，因此不会产生数据丢失问题，故不用加锁

# lab 7.3 barrier(moderate)

## 实验内容

为多线程实现barrier机制，达到线程同步的要求

## 实验流程

1. 大体流程为线程进入barrier后将bstate.nthread变量加1，再判断是否所有线程进入barrier（），若不是，进入睡眠等待唤醒，若是，则将bstate.nthread清0，轮次数bstate.round加1，唤醒所有睡眠的线程，以达到线程同步的要求
2. 注意多线程均要读写bstate.nthread变量并判断，可能会产生race-condition，因此要使这个操作具备原子性，就需要进行加锁，另外，读写bstate.nthread并判断，再进行睡眠或唤醒也需要具备原子性，因为若线程A判断后决定进入睡眠，在进入睡眠之前线程B判断后进行了唤醒，而线程A在唤醒后进入睡眠，这就导致线程A不能被唤醒，导致lost wake-up问题，因此，读写bstate.nthread并判断，加上后续睡眠或者唤醒这一整个流程需要具备原子性，因此需要加锁
3. 在进入barrier后先获取锁，再将bstate.nthread变量加1，若还有线程未进入barrier（），则调用`pthread_cond_wait(&cond, &mutex)`函数，该函数会在cond条件下进入睡眠，并释放mutex锁，在醒来时重新获取锁；若所有线程均进入了barrier()，则将bstate.nthread清0，轮次数bstate.round加1，并唤醒所有在cond条件下睡眠的线程，最后释放锁；当睡眠的线程被唤醒后会重新获取锁，接着再释放锁。以此达到线程同步的目的
