# print a page table(easy)

## 知识点

1. RISV-V的逻辑地址寻址采用三级页表的形式，一级页表的条目PTE包含二级页表的物理地址，二级页表的PTE包含三级页表的物理地址，三级页表的PTE中的PPN加上12bit的offset为内存页的物理地址
2. PTE（page table entry），共有64bit，其中高10bit保留，低10bit为标志位，中间44bit为PPN（physical page number）
3. 10bit标志位中, PTE_V判断PTE是否有效，PTE_R为读权限，PTE_W为写权限，PTE_X为可执行权限，只有PTE指向内存页时，读，写，可执行权限才会被设置，在一，二级页表中这三位均为0，故可以此判断此PTE所在的页表是否为叶子页表，即第三级页表

# A kernel page table per process (hard)

## 知识点

1. uvmunmap取消虚拟地址与物理地址的映射，若do_free = 0，那么三级页表中指向pte的指针即*pte被置零，这样就无法通过walk函数由虚拟地址找到指向pte的指针，也就得不到物理地址，但是pte的值没有清零释放，因此需要freewalk进一步置零pte，释放物理内存。但是执行freewalk之前要确保叶子页表的pte被置零，否则会出错，所以根据虚拟首地址及大小执行依次uvmunmap，do_free设置为1，释放叶子页表的物理内存

## 整体流程

1. 当进程在内核执行时，为每一个进程维护一个内核页表，运行时使用进程的内核页表，进程切换时也切换内核页表，没有进程运行时使用全局内核页表，每个进程的内核页表都应该与当前现有的全局内核页表保持一致
2. 为每个进程维护一个内核页表，故在struct proc中为进程的内核页表添加一个结构体成员 proc_kernel_pagetable

   ```
   pagetable_t proc_kernel_pagetable //add a kernel pagetable in process
   ```
3. 在分配进程控制块时对进程的结构体指针进行初始化，此时添加进程的内核页表，参考kvminit添加为进程建立内核页表的函数，并在allocproc函数中调用

   ```
   <vm.c>
   pagetable_t
   pkvminit(void)
   {
     pagetable_t proc_kernel_pagetable;
     proc_kernel_pagetable = (pagetable_t) kalloc();
     memset(proc_kernel_pagetable, 0, PGSIZE);

     // uart registers

     if(mappages(proc_kernel_pagetable, UART0, PGSIZE, UART0, PTE_R | PTE_W) != 0)
       panic("pkvmmap uart0");

     // virtio mmio disk interface

     if(mappages(proc_kernel_pagetable, VIRTIO0, PGSIZE, VIRTIO0, PTE_R | PTE_W) != 0)
       panic("pkvmmap virtio0");

     // CLINT

     if(mappages(proc_kernel_pagetable, CLINT, 0x10000, CLINT, PTE_R | PTE_W) != 0)
       panic("pkvmmap clint");

     // PLIC

     if(mappages(proc_kernel_pagetable, PLIC, 0x400000, PLIC, PTE_R | PTE_W) != 0)
       panic("pkvmmap plic");

     // map kernel text executable and read-only.
     if(mappages(proc_kernel_pagetable, KERNBASE, (uint64)etext-KERNBASE, KERNBASE, PTE_R | PTE_X) != 0)
       panic("pkvmmap kernel text");

     // map kernel data and the physical RAM we'll make use of.
     if(mappages(proc_kernel_pagetable, (uint64)etext, PHYSTOP-(uint64)etext, (uint64)etext, PTE_R | PTE_W) != 0)
       panic("pkvmmap kernel data");
     // map the trampoline for trap entry/exit to
     // the highest virtual address in the kernel.
     if(mappages(proc_kernel_pagetable, TRAMPOLINE, PGSIZE, (uint64)trampoline, PTE_R | PTE_X) != 0)
       panic("pkvmmap trampoline");
     return proc_kernel_pagetable;
   }

   <proc.c>
   found:
     p->pid = allocpid();

     // allocate and map the proc_kernel_pagetable，
     p->proc_kernel_pagetable = pkvminit();
   ```
4. 参考procinit，在allocproc中向进程的内核页表中添加进程内核栈的映射，然后移除在procinit中关于进程内核栈的建立与映射，代码添加到建立内核页表之后

   ```
   //allocate a page for process kernel stack, refer to procinit()
     //在进程的内核页表中添加进程内核栈的映射
     char *pa = kalloc();
     if(pa == 0){
       panic("kalloc");
     }
     uint64 va = KSTACK((int) (0));
     //uint64 va = KSTACK((int) (p - proc));
     if(mappages(p->proc_kernel_pagetable, va, PGSIZE, (uint64)pa, PTE_R | PTE_W) != 0)
       panic("proc kernel stack map");
     p->kstack = va;
   ```
5. 参考kvminithart，在schedule中获取到可运行进程后启动进程的内核页表，并在进程运行结束后切换到全局内核进程，且如果没有找到可运行的进程，也应该启动全局内核页表

   ```
    //将进程的内核页表写入SATP寄存器
           w_satp(MAKE_SATP(p->proc_kernel_pagetable));
           //使能页表并刷新TLB
           sfence_vma();

           swtch(&c->context, &p->context);

           //运行完毕后切换内核页表
           kvminithart();
   ```
6. 在freeproc中释放一个进程的内核页表，注意要先取消映射，再释放

   ```
   //取消内核栈映射并释放内核栈
     uvmunmap(p->proc_kernel_pagetable, p->kstack, 1 , 1);
     //释放进程的内核页表
     if(p->proc_kernel_pagetable)
       free_proc_kernelpagetable(p->proc_kernel_pagetable);
     p->kstack = 0;
     p->proc_kernel_pagetable = 0;
   ```

   由于不释放叶子页表的物理内存，故不能使用free_pagetable，在vm.c重新定义释放函数free_proc_kernelpagetable，如下

   ```
   <vm.c>
   // Free a process's kernel  pagetable, and free the
   // physical memory it refers to.
   void
   free_proc_kernelpagetable(pagetable_t pagetable)
   {
     //取消映射
     uvmunmap(pagetable, UART0, 1, 0);
     uvmunmap(pagetable, VIRTIO0, 1, 0);
     uvmunmap(pagetable, CLINT, (uint64)(0x10000 / PGSIZE), 0);
     uvmunmap(pagetable, PLIC, (uint64)(0x40000 / PGSIZE), 0);
     uvmunmap(pagetable, KERNBASE, (uint64)(((uint64)etext-KERNBASE) / PGSIZE), 0);
     uvmunmap(pagetable, (uint64)etext, (uint64)((PHYSTOP-(uint64)etext) / PGSIZE), 0);
     uvmunmap(pagetable, TRAMPOLINE, 1, 0);

     //释放页面
     freewalk_proc_kernelpgtbl(pagetable);
   }
   //释放进程的内核页表所需的freewalk，不要求叶子页表已经被释放
   void
   freewalk_proc_kernelpgtbl(pagetable_t pagetable)
   {
     // there are 2^9 = 512 PTEs in a page table.
     for(int i = 0; i < 512; i++){
       pte_t pte = pagetable[i];
       if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){ //如果 PTE 有效并且没有设置任何权限位（PTE_R、PTE_W 或 PTE_X），
                                                             // 这意味着该 PTE 指向一个更低级别的页表
         // this PTE points to a lower-level page table.
         uint64 child = PTE2PA(pte);
         freewalk_proc_kernelpgtbl((pagetable_t)child);
         pagetable[i] = 0;
       }
     }
     kfree((void*)pagetable);
   }
   ```
7. 由于使用进程的内核页表，故使用kvmpa函数时应该对应该进程的内核页表而不是全局内核页表，故进行修改

   ```
   pte = walk(myproc()->proc_kernel_pagetable, va, 0); //修改后
   ```

## 流程解释

1. 为进程生成内核页表，且需要与全局内核页表保持一致，故可考虑实现一个修改版的kvminit（），那么何时生成进程的内核页表呢，答案是在分配进程控制块时，即allocproc函数，也就是使用该进程时才去创建进程的内核页表，并完成映射
2. 在进程运行时会用到进程的内核栈（用于处理上下文及中断或异常），为进程映射内核栈是在操作系统启动时进行的(procinit)，因为内核栈的映射关系存放在全局内核页表当中，故boot时会初始化进程列表，并为每个进程映射内核栈，现在实验要求进程在内核运行时使用进程的内核页表，故需要在进程的内核页表添加进程内核栈的映射关系，此操作在创建进程内核页表并添加映射时进行，故参考procinit，在allocproc中添加关于内核栈的映射
3. 在添加完成进程的内核页表后，思考何时初始化页表寄存器（SATP），启动页表机制告诉硬件启动虚拟地址到物理地址的转换，我们不能在分配进程控制块时启动，因为此时进程还未运行，如果此时另一个进程处于运行态，更改SATP的值会造成错误（运行态使用的内核页表不属于该进程），实验要求提到在进程切换时切换内核页表，故在进程调度器scheduler内获取到运行态进程后启动进程的内核页表，参考kvminithart在scheduler函数内启动页表机制
4. 在没有进程运行时及时更换全局内核页表 kernel pagetable，故在schedule中，如果没有找到可运行的进程，就启动全局内核页表
5. 进程结束后需要释放进程的内核页表，因此在freeproc中添加释放页表的代码。释放页表一是要取消映射（uvmunmap），二是释放页面（freewalk）。根据实验要求，不必释放叶子物理内存界面，虽然不知道为什么，但do_free设置为0。注意，应该先取消内核栈映射并释放内核栈物理内存，再释放进程的内核页表

## 遇到的问题

1. 在freeproc中释放进程的内核页表时需要先取消映射，而建立Kernel text及Kernel data等映射时，其映射大小取决于etext[]，这在boot时得到，因此取消映射时无法得知大小。
   解决方法：在vm.c中定义释放进程的内核页面的函数，使用etext[]等计算取消映射的大小
2. 在初始化进程表时创建了进程的内核栈并映射到内核页面中，而进程的内核页面也建立了关于内核栈的映射，造成冲突
   解决方法：删去初始化进程表时分配并映射内核栈的代码
3. 创建进程的内核页表出现错误，make qeum出现以下错误

   ```
   scause 0x000000000000000d
   sepc=0x0000000080001072 stval=0x00000000000007f8
   panic: kerneltrap

   ```
   使用GDB进行调试，发现错误出现在userinit函数即建立第一个进程时，分配进程控制块时，进行内核栈的映射时发生错误，如下：

   ```
   if(mappages(p->proc_kernel_pagetable, va, PGSIZE, (uint64)pa, PTE_R | PTE_W) != 0) 
   ```
   继续往下寻找,错误发生在mappages函数内的walk调用内

   ```
   if((pte = walk(pagetable, a, 1)) == 0)
   ```
   寻找到错误发生在一个判断语句上，似乎错误不在这里，尝试在内核栈映射之前打印进程的内核页表，出现相同的错误 panic: kerneltrap，且打印的进程内核页表的物理地址为0，似乎是创建进程的内核页表出现错误。在创建进程的内核页表函数内映射之前打印页表地址，在创建进程的内核页表函数完成后打印页表地址，发现两者地址不同，故问题可能出现在映射。

   检查映射后发现映射没有问题，找出了问题所在，这是之前用来创建进程内核页表的函数:

   ```
   pkvminit(p->proc_kernel_pagetable);
   ```
   这里采用的值传递的方式，函数执行后，p->proc_kernel_pagetable不会改变，改变的是传递进去的副本，改变函数的形式，如下：

   ```
   p->proc_kernel_pagetable = pkvminit();
   ```
   让pkvminit（）返回一个页表地址，此错误解决。
4. 出现panic: kvmpa错误，使用GDB找出错误地址，发现错误出现在proc.c文件内的scheduler函数内，出错位置为切换上下文处，如下

   ```
   swtch(&c->context, &p->context);
   ```
   继续用GDB单步调试进行寻找，出错位置寻找如下

   ```
   disk.desc[idx[0]].addr = (uint64) kvmpa((uint64) &buf0);
   ```
   其中buf0处于内核栈上，查找buf0的物理地址时，找到的PTE无效导致panic("kvmpa")，由于Kvmpa查找是基于全局内核页表，而之前删除了全局内核页表中关于每个进程的内核栈的映射，故怀疑出错原因是这个

   但认真思考后，考虑到进程运行过程中应该使用自己进程内核页表映射的内核栈，故考虑修改Kvmpa函数，修改前函数是基于全局内核页表查询物理内存地址，

   ```
   pte = walk(kernel_pagetable, va, 0);
   ```
   修改后改为基于当前进程的内核页表查询，如下为修改之后

   ```
   pte = walk(myproc()->proc_kernel_pagetable, va, 0);
   ```
   这时候又出现编译报错如下

   ```
   kernel/vm.c:184:22: error: dereferencing pointer to incomplete type 'struct proc'
     184 |   pte = walk(myproc()->proc_kernel_pagetable, va, 0);
   ```
   这是因为vm.c文件中没有出现struct proc的定义，故将该结构体定义的头文件引用过来proc.h

   接着又出现新的错误如下

   ```
   kernel/proc.h:87:19: error: field 'lock' has incomplete type
      87 |   struct spinlock lock;
         |                   ^~~~
   ```
   这是因为在proc.h中定义结构体proc时，用到了另一个结构体spinlock，而proc.h并没有引用定义该结构体的头文件，故在vm.c中也引用其头文件 spinlock.h,到此，操作系统启动成功
5. 测试usertests程序时，出现错误 **panic: uvmunmap: not mapped**，通过GDB调试找出出错位置,

   发现出错位置是测试程序内的countfree函数，结合错误提示信息，应该是调用系统调用sbrk分配内存时出现错误，由报错信息发现出错位置在uvmunmap函数内，而通过printf测试排查sys_sbrk后发现错误不在这里面发生，再次单步调试，发现错误出现在countfree函数中父进程的等待函数wait（）处，由于wait函数内有freeproc，对应错误提示信息，进一步通过printf验证这一点，在freeproc前后一行设置printf验证，验证结果显示错误出现在此处的freeproc，进入函数内部进一步通过printf找出出错的代码，发现是取消内核栈映射出现错误，错误原因是参数设置错误，npages设置为4096，正确应该是1，修改后错误消失，
6. 再次测试出现错误panic: freewalk: leaf，按照上个错误顺序，并通过验证确定错误发生位置是释放进程的内核页表出现错误，结合错误信息，是freewalk出现错误，原因在于freewalk执行前要求所有叶子页表的pte均被移除，即释放叶子页表的物理内存，而我的代码中缺少这一步，而考虑到一点，若所有叶子页表的pte被移除，那么内核运行所需的关键物理页也将被释放，此时内核会崩溃，故不能释放叶子页表的物理内存，考虑新建函数freewalk_pro_kernelpgtbl，参考freewalk修改,错误解决
7. 新错误 panic: kfree，经过查询发现释放0x0000000010000000地址的物理内存时出现错误，取消物理设备映射时do_free设置为1，可能释放了叶子页表的物理内存时，内存未对齐导致，而这里不要求释放叶子页表的物理内存，故do_free设置为0
8. 测试execout出现错误mappages failed，这是execout程序的输出，execout要求先把内存耗尽，再释放一部分，因此会出现mappages failed，正常现象
9. 测试reparent2时出现scause 0x000000000000000c
   sepc=0x0000000080005ef0 stval=0x0000000080005ef0
   panic: kerneltrap，这个错误出现的原因是一般出现了页表映射丢失的情况，后检查发现在进程调度函数scheduler中，当进程运行完毕后，应该及时切换到内核页表，原代码缺少这一步骤导致错误
