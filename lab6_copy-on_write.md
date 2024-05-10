# copy-on write（hard）, 写时复制分配

## 实验内容

copy-on-write (COW) fork()的目标是推迟到子进程实际需要物理内存拷贝时再进行分配和复制物理内存页面。

COW fork()只为子进程创建一个页表，用户内存的PTE指向父进程的物理页。COW fork()将父进程和子进程中的所有用户PTE标记为不可写。当任一进程试图写入其中一个COW页时，CPU将强制产生页面错误。内核页面错误处理程序检测到这种情况将为出错进程分配一页物理内存，将原始页复制到新页中，并修改出错进程中的相关PTE指向新的页面，将PTE标记为可写。当页面错误处理程序返回时，用户进程将能够写入其页面副本。

COW fork()将使得释放用户内存的物理页面变得更加棘手。给定的物理页可能会被多个进程的页表引用，并且只有在最后一个引用消失时才应该被释放。

## 难点及解决方案

主要难点在于释放物理页面，给定的物理页可能会被多个进程的页表引用，并且只有在最后一个引用消失时才应该被释放。

解决方法：

1. 维护一个宏pa2pgref（pa），通过物理地址查找该页面的引用数，要想实现这个宏，还需要一个中间数组pa2pgref_id,即通过物理地址查找该页面的id，将此id作为另一个数组pageref的索引，pageref数组存放各个物理page的引用数量，数组索引就是之前得到的页面id，这样pa2pgref(pa）这个宏就可以根据物理地址得到该页面的引用数量
2. 为了防止多个进程同时修改pageref数组，定义一把自旋锁，确保同一时间只有一个进程修改pageref数组。
3. 在物理页面的生命周期内，添加对引用数的计数，主要有以下几个操作：

* kalloc（）：分配物理页，引用数设置为1，
* kfree（）：引用数减一，若为0，回收物理空间，
* 创建子进程时：父进程的引用数加1（子进程此时并没有实际分配物理页，故不存在引用数，若子进程又创建子进  程，根据物理地址得到的还是最开始父进程的物理页），引用数于物理地址绑定；
* 写时复制物理页时：创建新的物理页（引用数设置为1），旧的物理页引用数减1，但是若旧的物理页只有1，即此页面虽然是cow页面，但只有一个进程引用，此时无需单独创建新的物理页，只需要设置PTE_W位并清除PTE_COW位即可，该物理页的引用数仍为1

## 实验流程

1. 修改uvmcopy，时子进程的虚拟地址映射到父进程的物理页，二者共用一个物理页，但二者pte页表项中的PTE_W需要清除，且设置PTE_COW标志位
2. 修改usertrap，识别到页面错误时，首先根据PTE_COW判断该页面是否是cow页面，若是，则分配一个新的页面，然后复制旧页面到新页面，再取消该虚拟地址的映射，并重新映射，同时设置PTE_W标志位并清除PTE_COW位
3. 在kalloc.c维护一个数组，存放物理页的引用，具体实现见难点内的解决方案
4. 在kalloc.c中，在kinit函数内初始化锁，在kalloc函数内，若成功分配到物理页，引用数设置为1；在kfree内，释放物理页时，检查引用数，若为0，回收空间。同时定义kcowref函数，用于cow页的写时复制，函数检查该物理页引用数是否为1，若为1，无需分配新页，返回其物理地址即可，若大于1，创建新页，把旧页内容复制到新页，旧页引用减一，返回新页地址。
5. 为提高代码可复用性，在vm.c中定义函数uvmcowcopy，用于发生cow页面错误时复制cow页；定义函数uvmcheckcowpage用于检测是否是cow页面，并替换usertrap内的代码
6. 修改copyout，在遇到COW页面时使用与页面错误相同的方案在copyout作usertrap相同处理、

## 遇到的问题

1. 仅修改uvmcopy和usertrap后在Xv6启动时出现错误

   ```
   usertrap(): unexpected scause 0x0000000000000005 pid=1
               sepc=0x000000000000036c stval=0x000000000000036c
   panic: init exiting
   ```
   撤销修改后错误消除，经排查问题出现在下面这两行代码

   ```
   flags = (PTE_FLAGS(*pte) & ~PTE_W) | PTE_COW; //设置标志位,并清零PTE_W（标记为不可写）,RSW位（bit8）置1，标记为COW页
   *pte = (*pte) & flags;            //修改原进程pte项的标志位
   ```
   原因在于pte一共53位，第一行设置标志位后得到的flags类型是uint，占32位，且只有低10位有效，其余高位均被清零，在第二行代码中，(*pte) & flags会清除pte中除了低10位之外的所有其他位，因此导致pte页表项被破坏，造成启动错误，现在修改这两行代码：

   ```
   *pte = (*pte & ~PTE_W) | PTE_COW; //清除原进程pte的PTE_W标志位，并设置PTE_COW标志位
    flags = PTE_FLAGS(*pte);          //获取flags，用于新进程的pte
   ```
   Xv6正常启动
2. 测试程序cowtest，出现错误panic: remap，通过printf打印发现错误发生在测试程序正式执行之前，因此错误发生在shell进程fork出子进程进而调用exec运行测试程序这个过程中，由错误信息得知是因为重新映射，原因在于在uvmcopy和usertrap均进行了mappage如下：

   ```
   <uvmcopy>
   if(mappages(new, i, PGSIZE, pa, flags) != 0){   //不分配新物理内存，映射到同一个物理页

   <usertrap>
   if(mappages(p->pagetable, va, PGSIZE, new_pa, flags) != 0)

   ```
   前者意图是将新进程的所有虚拟地址映射到就进程的物理页面，后者意图是将发生错误的相关pte映射到一个重新分配的物理页面，因此发生了重新映射的错误，这里在第二个映射之前先取消原映射，再重新映射即可

   ```
   <usertrap>
   uint flags = (PTE_FLAGS(*pte) | PTE_W) & ~PTE_COW;
   uvmunmap(p->pagetable, PGROUNDDOWN(va), 1, 0);
   if(mappages(p->pagetable, PGROUNDDOWN(va), PGSIZE, new_pa, flags) != 0){
       printf("usertrap: cow mappages failed\n");
       exit(-1);
   }

   ```
   修改后成功运行
3. 测试出现错误

   ```
   usertrap(): unexpected scause 0x0000000000000002 pid=3
               sepc=0x0000000000001004 stval=0x0000000000000000
   ```
   后续找了很久，终于在kalloc中新分配物理页，引用数要设置为1，而不是加1，且此时设置引用数时不要加锁，因为 kmem 的锁已经保证了同一个物理页不会同时被两个进程分配，并且在 kalloc() 返回前，其他操作 pageref() 的函数也不会被调用，因为没有任何其他进程能够在 kalloc() 返回前得到这个新页的地址。事实上加锁会导致出错`panic: acquire`，原因暂且未知
