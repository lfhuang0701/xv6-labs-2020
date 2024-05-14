// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

// struct {
//   struct spinlock lock;
//   struct run *freelist;
// } kmem;

//roster new add
struct{
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU];

//roster new add
char *kmem_lock_names[] = {
    "kmem_cpu_0",
    "kmem_cpu_1",
    "kmem_cpu_2",
    "kmem_cpu_3",
    "kmem_cpu_4",
    "kmem_cpu_5",
    "kmem_cpu_6",
    "kmem_cpu_7",
};

void
kinit()
{
  // initlock(&kmem.lock, "kmem");

  //roster new add
  for(int i = 0; i < NCPU; i++){
    initlock(&kmem[i].lock, kmem_lock_names[i]);
  }

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

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  //roster new add
  int cpu = cpuid();
  push_off();
  acquire(&kmem[cpu].lock);

  r->next = kmem[cpu].freelist;
  kmem[cpu].freelist = r;
  release(&kmem[cpu].lock);

  pop_off();
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  // roster new add
  int cpu = cpuid();
  push_off();
  acquire(&kmem[cpu].lock);
  r = kmem[cpu].freelist;
  if(r){
    kmem[cpu].freelist = r->next;
    release(&kmem[cpu].lock);
  }
  else{
    release(&kmem[cpu].lock);
    // struct run *steal_r;
    //struct run *steal_r_end = 0;
    // int steal_pages = 64;
    for(int i = 0; i < NCPU; i++){
      if(i == cpu)
        continue;
      acquire(&kmem[i].lock);
      // struct run *cur_r = kmem[i].freelist;
      // while(cur_r && steal_pages){
      //   // if(steal_pages == 64)
      //   //   steal_r_end = cur_r; //保存偷来的空白链表的尾部

      //   cur_r->next = steal_r;
      //   steal_r = cur_r;
      //   steal_pages--;
      // }
      // if(!steal_pages) break;
      r = kmem[i].freelist;
      if(r){
        kmem[i].freelist = r->next;
        release(&kmem[i].lock);
        break;
      }
      release(&kmem[i].lock);
    }
    // if(steal_r == steal_r_end){
    //   r = steal_r;
    // }
    // else { //偷来的空闲页表数量大于1
    //   acquire(&kmem[cpu].lock);
    //   steal_r_end->next = kmem[cpu].freelist;
    //   kmem[cpu].freelist = steal_r->next;
    //   release(&kmem[cpu].lock);
    //   r = steal_r;
    // }
  }

  pop_off();

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
