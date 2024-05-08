# lab5.1 lazy allocation

## 实验内容

1. 实现延迟分配用户空间堆内存
2. sbrk（）分配内存时不分配物理内存，只是记住分配了哪些用户地址，并在用户页表中将这些地址标记为无效。当进程第一次尝试使用延迟分配中给定的页面时，CPU生成一个页面错误（page fault），内核通过分配物理内存、置零并添加映射来处理该错误

## 知识点

1. 为Xv6添加延迟分配特性，提高分配效率
2. 结合pagt table和trap机制，实现在trap内分配内存，更巧妙使用虚拟内存

## 实验流程

1. 修改sys_sbrk，使其不分配物理内存，只增大用户地址
2. 添加发生用户页面错误时分配物理内存的代码，在trap.c/usertrap中，若发生用户页面错误，则r_scause = 15，使用r_stval获取发生错误的虚拟地址，向下进行页面对齐，然后申请一个page的物理空间，映射到已经页面对齐的虚拟地址。若申请物理内存或者映射失败，应该退出该进程并打印错误。
3. 因为存在部分虚拟地址未映射到物理内存，故uvmunmap()中查询到无效PTE时会导致系统panic崩溃，修改该部分代码，使其正常运行

# lab5.2 Lazytests and Usertests (moderate)

## 知识点难点

1. 进一步熟悉页面错误发生的流程及系统调用发生的流程
2. 进一步了解到walk函数的查询机制，了解页表机制
3. 发生页面错误时，需要检查虚拟地址是否在合法范围内，即不能超过堆实际分配的大小p->sz，也不能低于用户栈页面（因为用户栈页面下是guard page，不允许映射），使用 `PGROUNDDOWN(p->trapframe->sp)`获取用户栈的栈顶（也就是guard page的最大地址）
4. 若向系统调用传递懒分配的地址，在内核态使用未分配的内存，会陷入kerneltrap，而不会进入usertrap，因此不能进行用时分配，需要在内核态使用这些内存之前进行物理分配。内核态访问用户空间时，需要先使用copyin函数从用户空间拷贝数据，copyin函数内使用walkaddr函数翻译虚拟地址，若虚拟地址是懒分配内存，翻译会出现错误，因此在walkaddr内判断虚拟地址是否是懒分配内存地址，若是，则分配物理内存。

## 实验内容

* 处理`sbrk()`参数为负的情况。
* 如果某个进程在高于`sbrk()`分配的任何虚拟内存地址上出现页错误，则终止该进程。
* 在`fork()`中正确处理父到子内存拷贝。
* 处理这种情形：进程从`sbrk()`向系统调用（如`read`或`write`）传递有效地址，但尚未分配该地址的内存。
* 正确处理内存不足：如果在页面错误处理程序中执行`kalloc()`失败，则终止当前进程。
* 处理用户栈下面的无效页面上发生的错误。

## 实验流程

1. 应对`sbrk()`参数为负的情况，添加下面代码

   ```
   if(n < 0){
       uvmdealloc(p->pagetable, addr, addr + n);
     }
   ```

   `sbrk()`参数为负，表示要减小用户地址空间，应该对应的取消映射并释放物理内存
2. 如果某个进程在高于`sbrk()`分配的任何虚拟内存地址上出现页错误，则终止该进程，在usertrap内，发生页面错误后，获取发生错误的虚拟地址va，判断是否超出分配的虚拟内存地址，若超出，则应该终止进程

   ```
   if(va > myproc()->sz)
         exit(-1);
   ```
3. 在`fork()`中正确处理父到子内存拷贝，在内存拷贝中，uvmcopy会检查pte条目的有效性，由于部分虚拟内存地址尚未分配物理内存，故pte条目无效，类比于上个实验uvmunmap中的处理方法，取消系统panic

   ```
   if((pte = walk(old, i, 0)) == 0)
         continue;
   if((*pte & PTE_V) == 0)
         continue;
   ```
4. 若发生页面错误时查询到的错误虚拟地址va处于用户栈下面的guard page，应该正确处理，guard page是为了防止栈溢出，因此不需要进行物理内存的映射，属于无效界面，因此检查虚拟地址va是否guard page，若是，则应该终止当前进程，因为程序试图修改guard page

   ```
   if(va >= myproc()->sz || va <= PGROUNDDOWN(p->trapframe->sp))
         p->killed = 1;
   ```
5. 在内核态使用这些懒分配的内存之前进行物理分配，在walkaddr内添加代码：

   ```
   if(pte == 0 || (*pte & PTE_V) == 0){

       struct proc* p = myproc();

       // 如果某个进程在高于sbrk()分配的任何虚拟内存地址上出现页错误，则终止该进程
       if(va >= p->sz || va <= PGROUNDDOWN(p->trapframe->sp))
         return 0;

       va = PGROUNDDOWN(va);

       if((pa = kalloc()) == 0){ //物理内存分配失败
         printf("out of memory\n");
         return 0;
       }

       memset(pa, 0, PGSIZE);
       if(mappages(myproc()->pagetable, va, PGSIZE, pa, PTE_W|PTE_X|PTE_R|PTE_U) != 0){
         kfree(pa);
         printf("lazyalloc: mappages() failed\n");
         return 0;
       }
       return pa;
     }
   ```

## 遇到的问题

1. running test lazy alloc
   panic: uvmunmap: walk，测试第一个test lazy alloc时出现系统panic，错误显示没有找到pte，也就是取消映射时的虚拟地址找不到对应的pte条目，打印出错的虚拟地址为通过`0x0000000040000000`，而sbrk分配后虚拟p->sz为 `0x0000000040003000`（通过在进入exit(0)时打印进行验证），print定位发现错误发生在测试退出执行`exit（0）`时，取消映射找不到pte页表项，由于懒分配没有分配页表项及物理内存，故出现上述错误，修改代码忽略上述两种情况

   ```
   if((pte = walk(old, i, 0)) == 0)
         continue;
   if((*pte & PTE_V) == 0)
         continue;
   ```
   第一个continue应对的情况是pte页表项为0，也就是不存在，参考walk函数，查询一，二级页表时，若pte无效（PTE_V = 0），且只查询不分配，则walk函数返回0，因此，若懒分配的虚拟内存是一，二级页表地址，此时没有分配物理内存，也就找不到下一级页表，此时就会返回0，

   第二个continue应对的情况是pte页表无效，参考walk函数，若虚拟地址对应的一，二级页表中的页表项均有效，则直接返回虚拟地址对应的叶子页表的pte页表项，而不进行有效性检查，因此，若虚拟地址中对应叶子页表的虚拟地址是懒分配，则会出现这种情况
2. 测试out of memory出现错误`panic: walk`，在walk函数内

   ```
   if(va >= MAXVA)
       panic("walk");
   ```
   因此出错原因是因为分配的虚拟内存地址超出了MAXVA即最大虚拟内存地址，导致系统崩溃，因此在sys_sbrk函数内要添加扩展内存的限制,由此启发，对于缩小内存也应该有所限制，使其不低于0，因为用户进程的地址空间是[0，MAXVA]
3. 测试lazy unmap出现问题`panic: freewalk: leaf`，原因在于在trap内检查虚拟地址va时，判断逻辑出现问题，虚拟地址不在范围内没有直接终止进程，导致映射了其他内存，进而导致释放时没能释放
