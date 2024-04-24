# print a page table

## 知识点

1. RISV-V的逻辑地址寻址采用三级页表的形式，一级页表的条目PTE包含二级页表的物理地址，二级页表的PTE包含三级页表的物理地址，三级页表的PTE中的PPN加上12bit的offset为内存页的物理地址
2. PTE（page table entry），共有64bit，其中高10bit保留，低10bit为标志位，中间44bit为PPN（physical page number）
3. 10bit标志位中, PTE_V判断PTE是否有效，PTE_R为读权限，PTE_W为写权限，PTE_X为可执行权限，只有PTE指向内存页时，读，写，可执行权限才会被设置，在一，二级页表中这三位均为0，故可以此判断此PTE所在的页表是否为叶子页表，即第三级页表
