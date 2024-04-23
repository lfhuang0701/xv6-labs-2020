# trace

## 实现功能及难点

1. 追踪指定系统调用号的系统调用，并打印系统调用的信息
2. 难点在于熟悉系统调用的全流程，运用用户空间传递给内核空间的指针（proc）

## 整体流程

1. 在user/user.h声明用户态函数trace并在user/trace.c定义函数，实现用户态的函数，并在Makefile的UPROGS中添加\$U/\_trace
2. 在**user/usys.pl**添加entry("trace"),此脚本运行后会生成usys.S汇编文件，里面定义了每个系统调用的用户态跳板函数

   ```
   trace:		# 定义用户态跳板函数
    li a7, SYS_trace	# 将系统调用 id 存入 a7 寄存器
    ecall				# ecall，调用 system call ，跳到内核态的统一系统调用处理函数 syscall()  (syscall.c)
    ret
   ```
3. 在系统调用头文件syscall.h中添加sys_trace即trace的系统调用号
4. 在系统调用文件syscall.c中的函数数组指针中添加sys_trace，可通过系统调用号映射到相应内核调用函数，并用extern全局声明内核调用函数sys_trace，为方便通过系统调用号找到对应系统调用名称，添加字符串数组syscallNames[].
5. 由于sys_trace与进程有关，因此在sysproc中定义sys_trace，定义如下，sys_trace的主要作用是获取用户态trace函数传来的参数，通过argint，argraw从trapframe中的a0寄存器获取，并将此参数作为当前进程的追踪掩码（traceMask）的值，myproc()用于获取当前进程上下文，执行exec系统调用时，会获取当前进程上下文（exec调用的
   进程与trace位于同一进程），此时可通过追踪掩码进行判断是否追踪该进程。

   ```
   //设置当前进程的追踪掩码
   uint64
   sys_trace(void){
     int mask;
     if(argint(0, &mask) < 0){  //myproc->trapfram->a0,及当前进程从用户态传来的第一个参数，在trace系统调用中，传递的是追踪掩码，
       return -1;
     }
     myproc()->traceMask = mask;
     return 0;
   }
   ```
6. 在kernel/proc.h的proc结构体（保存当前进程信息）中添加追踪掩码（traceMask）,并在新建进程函数（allocproc（））中初始化traceMask为0，防止出错，为实现trace进程时一并trace子进程，故在fork（）函数内将父进程的traceMask复制给子进程。
7. 最后在kernel/syscall.c中完善syscall(）函数，所有的系统调用都会进入此函数，因此需判断当前系统调用是否被追踪，若是，则打印当前系统调用的信息

   ```
   void
   syscall(void)
   {
     int num;
     struct proc *p = myproc();

     num = p->trapframe->a7;
     if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {
       p->trapframe->a0 = syscalls[num]();     //将内核态系统调用的返回值返回给用户态
       if((1 << num) & p->traceMask){          //检查当前进程是否被追踪
           printf("%d: syscall %s -> %d\n", p->pid, syscallNames[num], p->trapframe->a0);
       }

     } else {
       printf("%d %s: unknown sys call %d\n",
               p->pid, p->name, num);
       p->trapframe->a0 = -1;
     }
   }
   ```

# sysinfo

## 实现功能及难点

1. 添加一个系统调用，返回空闲内存及已经创建的进程数量，
2. 难点在于要从内核空间拷贝一个结构体（sysinfo）到用户空间，
3. 以及添加计算空闲内存的函数与计算已创建的进程数量的函数
4. 了解xv86的内存管理机制，kernel/kalloc.c中内存分配函数kalloc()和内存释放函数kfree()

## 整体流程

1. 同trace实验一样，在user/user.h声明sysinfo()函数及sysinfo结构体，供测试程序使用，在user/usys.pl中添加entry("sysinfo"),实现用户态函数与内核态函数的连接，在kernel/syscall.h添加系统调用号，在kernel/syscall.c添加系统调用函数sys_sysinfo,
2. 由于需要获取空闲内存数量,故定义计算空闲内存函数
