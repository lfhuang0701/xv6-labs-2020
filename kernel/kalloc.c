// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

#define PA2PAGE_ID(p) (((p) - KERNBASE) / PGSIZE) //根据物理地址获取物理page索引
#define PAGE_MAX      PA2PAGE_ID(PHYSTOP)            //物理page最大数量

int pageref[PAGE_MAX];              //物理page引用数的数组
#define PA2PGREF(p)     pageref[PA2PAGE_ID((uint64)(p))]        //通过物理地址pa获取引用数 

struct spinlock pageref_lock;       //用于pageref的锁，防止多个进程同时修改

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

void
kinit()
{
  initlock(&kmem.lock, "kmem");

  //初始化锁
  initlock(&pageref_lock, "pageref");

  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  acquire(&pageref_lock);
  if(--PA2PGREF(pa) <= 0){  
  
    // Fill with junk to catch dangling refs.
    //printf("%p,kfree\n", pa);
    memset(pa, 1, PGSIZE);

    r = (struct run*)pa;

    acquire(&kmem.lock);
    r->next = kmem.freelist;
    kmem.freelist = r;
    release(&kmem.lock);
  } 
  release(&pageref_lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r){
    kmem.freelist = r->next;
  }
  release(&kmem.lock);

  if(r){
    memset((char*)r, 5, PGSIZE); // fill with junk
    //acquire(&pageref_lock);
    PA2PGREF(r) = 1;
    //release(&pageref_lock);
  }
  return (void*)r;
}

//物理page引用数加1
void kpageref_add(void* pa){
  //printf("%p ref+1\n", pa);
  acquire(&pageref_lock);
  PA2PGREF(pa)++;
  release(&pageref_lock);
}

//cow页面，根据引用数选择是否创建新页面
void* kcowref(void* pa){
  acquire(&pageref_lock);
  if(PA2PGREF(pa) <= 1){    //若cow页面仅有一个引用，无需创建新页面，直接修改标志位即可
    release(&pageref_lock);
    return pa;
  }

  void* new_pa = kalloc();
  if(new_pa == 0){
    release(&pageref_lock);
    return 0;     //内存耗尽
  }
  memmove(new_pa, pa, PGSIZE); //复制到新页面

  PA2PGREF(pa)--;           //旧页面引用减一

  release(&pageref_lock);

  return new_pa;
}