# lab 10 mmap

## 实验内容

实现linux系统调用mmap的简单版：支持将文件映射到用户空间地址内，并支持将对文件的修改写回磁盘

## 知识点

以上lab内容的整合实现，包括写入和读取磁盘文件，page fault 懒分配，系统调用

## 整体实现

1. 在用户地址空间寻找一块空闲区域用于映射

   ![User Address Space](https://blog.miigon.net/assets/img/mit6s081-lab10-useraddrspace.png)

   用户空间如上，堆区的空闲区域最大，因为用户使用堆区（heap）进行动态分配内存，且是自下而上的，因此需要映射到堆区的最高位置，且向下生长。应以trapframe作为mmapend
2. 在proc.h, 定义VMA（虚拟内存区域）结构体，包含mmap区域的信息，包含开始地址，大小，所映射的文件，文件内的偏移量，权限，文件标志位，vma有效位。用于秒速映射区域，方便后续其他处理。每个进程可以包含多个映射区域，因此在proc结构体添加VMA数组，数组大小定为16
3. 实现mmap系统调用，用户传入所映射文件的文件描述符fd，文件内的偏移量，映射区域的权限prot，映射区域的修改是否写入文件标志位flags等。需要先判断prot与文件的权限是否冲突，接着遍历当前进程的VMA数组，找到空闲的VMA，并不断更新最后一个VMA的结束地址vend，并将当前文件映射到vend的下面（VMA整体在堆区是自上而下生长，但是在每个VMA内部，映射是自下而上的，即开始地址是最低地址）。最后将传入的信息传递给VMA结构体，并增加文件的引用（以防文件被释放），最后返回映射区域的开始地址。
4. 对映射区域进行了懒分配，及映射时不分配物理地址，当发生page fault（页面错误）时再进行分配。在usertrap函数内，发生page fault时，先获取发生页面错误的虚拟地址，再进行分配。第一步先根据虚拟地址va找到对应的vma指针，此处定义find_vma函数，遍历vma数组找到对应的vma，得到vma指针后，先分配一页物理页，然后从磁盘文件读取一页内容到物理页，最后将物理页与虚拟地址建立映射，注意标志位。
5. mmap系统调用完成后，接着是munmap,取消映射，取消映射时需要判断是否将修改部分写入磁盘文件。用户传入取消映射的开始地址va及大小sz，munmap函数先根据va获取对应的vma，取消映射的区域只能位于vma区域的开始部分或结尾部分，不能处于中间空洞部分。此时va应该大于或等于v->vastart，若va大于v->vastart，进行页面对齐得到addr_aligned，接着计算实际取消映射的大小nunmap，最后调用自定义函数vmaunmap取消映射
6. vmaunmap函数为自定义函数，类似于uvmunmap函数，多余部分为将取消映射部分的修改写入磁盘文件。首先要获取取消映射部分在vma内的偏移量aoff，根据aoff判断取消映射的是不是vma内的最后一个页，然后进行不同操作。
7. vmaunmap执行完毕后，如果取消映射的部分位于vma开始位置，需要更新vma在文件内的偏移量offset和开始位置vastart，最后需要更新vma的大小v->sz，如果v->sz小于0，关闭对于文件的引用，并释放vma
8. allocproc函数内初始化进程的时候，初始化vma数组
9. freeproc释放进程时，调用vmaunmap释放vma内存。
10. fork子进程时，将父进程的vma数组拷贝到子进程，但不拷贝物理页，但需要增加文件的引用

## 遇到的问题

1. `find vma fail`，根据虚拟地址va寻找vma指针时错误，通过findvma函数内遍历vma数组时printf打印v->sz,发现全为0，应该是mmap系统调用错误,发现是映射时忘记设置v->sz信息,添加后错误消失
2. `panic: remap`，通过printf调试发现错误发生在第二次page fault，且发生page fault的虚拟地址va相同，进入到vmalazyalloc函数中进行映射时发生错误。原因在于映射物理页（mappages）时，标志位没有设置PTE_U，也就是是否可以被用户模式下的程序访问，设置后错误消失。
3. `mmaptest: mmap_test failed: mmap (2), pid=3`, mmap出现错误，通过printf检查发现错误是检查prot权限位时出现错误，试图打印文件 f 的readable和writeable时失败,`f->readable:panic: acquire`, 应该与锁有关，这里不打印此信息，转而检查if条件,修改if条件后此项测试通过
4. test mmap dirty
   mmaptest: mmap_test failed: file does not contain modifications, pid=3
   测试修改的内容是否写回磁盘文件时错误,经测试发现只有第一页被写回磁盘，从第二页开始全为0，检查后发现在自定义函数vmaunmap中计算aoff公式错误，修改后test mmap dirty 通过
5. 测试not-mapped unmap时，出现错误`panic: uvmunmap: not mapped`，原因在于此部分vma并没有实际分配物理页，也就不存在pte页表项，因此修改vmaunmap函数即可
