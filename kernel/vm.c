#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "spinlock.h"
#include "proc.h"


/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable;

extern char etext[];  // kernel.ld sets this to end of kernel code.

extern char trampoline[]; // trampoline.S

/*
 * create a direct-map page table for the kernel.
 */
void
kvminit()
{
  kernel_pagetable = (pagetable_t) kalloc();
  memset(kernel_pagetable, 0, PGSIZE);

  // uart registers
  kvmmap(UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  kvmmap(VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // CLINT
  kvmmap(CLINT, CLINT, 0x10000, PTE_R | PTE_W);

  // PLIC
  kvmmap(PLIC, PLIC, 0x400000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  kvmmap(KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  kvmmap((uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  kvmmap(TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
}

/*
 * create a direct-map page table for the proc_kernel_pagetable
 */
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
  //内核启动时用到CLINT，运行过程中不使用，用户进程的最大内存与此部分重叠，因此取消该部分映射
  // if(mappages(proc_kernel_pagetable, CLINT, 0x10000, CLINT, PTE_R | PTE_W) != 0)
  //   panic("pkvmmap clint");
  
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

// Switch h/w page table register to the kernel's page table,
// and enable paging.
void
kvminithart()
{
  w_satp(MAKE_SATP(kernel_pagetable));
  sfence_vma();
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
// PTE(Page Table Entry)
//虚拟地址只用了39bit，其中高27bit用作三个页表的索引，低12bit用作offset，
// 地址转换时，只需将虚拟内存地址的27bit翻译为最后一级页表的物理page号（44bit），即PPN，剩下12bitoffset直接拷贝即可
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  if(va >= MAXVA)
    panic("walk");

  for(int level = 2; level > 0; level--) {
    pte_t *pte = &pagetable[PX(level, va)];   //获取虚拟地址在当前级别页表中的PTE条目
    if(*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte);  //获取下一级页表的物理内存地址
    } else {
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0)   //如果当前PTE无效且设置alloc非0，分配一个新的页表并更新当前pte指向这个新的页表
      {  //printf("alloc %d pagetable %d\n", alloc, pagetable);
        return 0;}
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  return &pagetable[PX(0, va)];//循环结束后，pagetable为最低级页表，此时返回虚拟地址对应的pte
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
//将虚拟内存地址转为物理内存地址，其该地址用户可用
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if(va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if(pte == 0)
    return 0;
  if((*pte & PTE_V) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);
  return pa;
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void
kvmmap(uint64 va, uint64 pa, uint64 sz, int perm)
{
  if(mappages(kernel_pagetable, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

// translate a kernel virtual address to
// a physical address. only needed for
// addresses on the stack.
// assumes va is page aligned.
uint64
kvmpa(uint64 va)
{
  uint64 off = va % PGSIZE;
  pte_t *pte;
  uint64 pa;

  pte = walk(myproc()->proc_kernel_pagetable, va, 0);
  if(pte == 0)
    panic("kvmpa");
  if((*pte & PTE_V) == 0)
    panic("kvmpa");
  pa = PTE2PA(*pte);
  return pa+off;
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned. Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;

  a = PGROUNDDOWN(va);
  last = PGROUNDDOWN(va + size - 1);
  for(;;){
    if((pte = walk(pagetable, a, 1)) == 0)
      return -1;
    if(*pte & PTE_V)
    {
      printf("va %p\n", a);
      panic("remap");
    }
    *pte = PA2PTE(pa) | perm | PTE_V;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// Remove npages of mappings starting from va. va must be
// page-aligned. The mappings must exist.
// Optionally free the physical memory.
//取消从虚拟内存地址va开始npages个页表的映射,根据do_free判断是否释放叶子物理内存界面
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;

  if((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    if((pte = walk(pagetable, a, 0)) == 0)
      panic("uvmunmap: walk");
    if((*pte & PTE_V) == 0)
      panic("uvmunmap: not mapped");
    if(PTE_FLAGS(*pte) == PTE_V)
      panic("uvmunmap: not a leaf");
    if(do_free){
      uint64 pa = PTE2PA(*pte);
      kfree((void*)pa);
    }
    *pte = 0;
  }
}

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t
uvmcreate()
{
  pagetable_t pagetable;
  pagetable = (pagetable_t) kalloc();
  if(pagetable == 0)
    return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// Load the user initcode into address 0 of pagetable,
// for the very first process.
// sz must be less than a page.
void
uvminit(pagetable_t pagetable, uchar *src, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W|PTE_R|PTE_X|PTE_U);
  memmove(mem, src, sz);
}

// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
//增大进程内存大小，涉及分配物理内存及相应的页表项（PTE）
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  char *mem;
  uint64 a;

  if(newsz < oldsz)
    return oldsz;

  oldsz = PGROUNDUP(oldsz); //大小向上对齐，PGSIZE的倍数
  for(a = oldsz; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    int ret = mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_W|PTE_X|PTE_R|PTE_U);
    // if(mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_W|PTE_X|PTE_R|PTE_U) != 0){
    if(ret != 0){
      kfree(mem);
      //printf("ret %d\n", ret);
      printf("mappages failed\n");
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
//减小用户进程内存大小，涉及取消映射
uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if(newsz >= oldsz)
    return oldsz;
  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
  }

  return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
//递归释放页表中的所有页面,在释放页表之前，确保所有叶子映射（即最低一级，保留实际物理内存地址）已经被移除，因为此函数不会释放叶子界面的物理内存
//如果叶子映射未被移除，会出现错误
void
freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){ //如果 PTE 有效并且没有设置任何权限位（PTE_R、PTE_W 或 PTE_X），
                                                          // 这意味着该 PTE 指向一个更低级别的页表
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if(pte & PTE_V){   //如果是叶子界面，continue
      panic("freewalk: leaf");
    }
  }
  kfree((void*)pagetable);
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

// Free user memory pages,
// then free page-table pages.
void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  if(sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz)/PGSIZE, 1);
  freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if((*pte & PTE_V) == 0)
      panic("uvmcopy: page not present");
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc()) == 0)
      goto err;
    memmove(mem, (char*)pa, PGSIZE);
    if(mappages(new, i, PGSIZE, (uint64)mem, flags) != 0){
      kfree(mem);
      goto err;
    }
  }
  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void
uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  
  pte = walk(pagetable, va, 0);
  if(pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (dstva - va0);
    if(n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int
copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  
  return copyin_new(pagetable, dst, srcva, len);
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
//将一个以空字符（'\0'）终止的字符串从用户空间复制到内核空间。
int
copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  
  return copyinstr_new(pagetable, dst, srcva, max);
}

//vmprint使用到的递归函数
void
pgtblprint(pagetable_t pagetable, int depth)
{
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if(pte & PTE_V){
      printf("..");
      for(int j = 0; j < depth; j++){
        printf("..");
      }
      printf("%d: pte %p pa %p\n", i, pte, PTE2PA(pte));
    }
    if((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0){ //PTE有效且不是最后一级页表
      uint64 child = PTE2PA(pte); 
      pgtblprint((pagetable_t)child, depth+1);
    }
  }
}

void
vmprint(pagetable_t pagetable)
{
  printf("page table %p\n", pagetable);
  pgtblprint(pagetable, 0);
}

// Free a process's kernel  pagetable, and free the
// physical memory it refers to.
void
free_proc_kernelpagetable(pagetable_t pagetable)
{
  //取消映射
  uvmunmap(pagetable, UART0, 1, 0);
  uvmunmap(pagetable, VIRTIO0, 1, 0);
  // uvmunmap(pagetable, CLINT, (uint64)(0x10000 / PGSIZE), 0);
  uvmunmap(pagetable, PLIC, (uint64)(0x40000 / PGSIZE), 0);
  uvmunmap(pagetable, KERNBASE, (uint64)(((uint64)etext-KERNBASE) / PGSIZE), 0);
  uvmunmap(pagetable, (uint64)etext, (uint64)((PHYSTOP-(uint64)etext) / PGSIZE), 0);
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);

  //释放页面
  freewalk_proc_kernelpgtbl(pagetable);
}

//copy the old_pagetable to new_pagetable
int
copy_pagetable(pagetable_t old_pagetable, pagetable_t new_pagetable, uint64 start, uint64 sz)
{
  pte_t *pte;
  uint64 pa;
  int flags;
  for(uint64 i = PGROUNDUP(start); i < sz + start; i += PGSIZE)
  {
    if((pte = walk(old_pagetable, i, 0)) == 0)
      panic("copy_pagetable: pte donot exist");
    if((*pte & PTE_V) == 0)
      panic("copy_pagetable: pte unvaild");

    pa = PTE2PA(*pte);

    flags = PTE_FLAGS(*pte) & ~PTE_U; //内核无法访问设置了PTE_U的用户地址

    //向进程内核页表映射,使用同一个物理地址，因为两个映射都指向同处
    if(mappages(new_pagetable, i, PGSIZE, pa, flags) != 0){
      uvmunmap(new_pagetable, PGROUNDUP(start), (i - PGROUNDUP(start)) / PGSIZE, 0); //映射失败，取消之前的映射，但不释放实际的内存
      return -1;
    }

  }
  return 0;
}

//deallocate process kernel pagetable
uint64
kvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if(newsz >= oldsz)
    return oldsz;
  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){
    uint64 npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 0);
  }
  return newsz;
}