#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "sysinfo.h"

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

//设置当前进程的追踪掩码
uint64
sys_trace(void){
  printf("current:sys_trace\n");
  int mask;
  if(argint(0, &mask) < 0){  //myproc->trapfram->a0,即当前进程从用户态传来的第一个参数，在trace系统调用中，传递的是追踪掩码，
    return -1;
  }
  myproc()->traceMask = mask;
  return 0;
}

//计算当前的空闲内存及正在运行的进程数量，并拷贝到用户空间
uint64
sys_sysinfo(void){
  
  //以指针的形式读取系统调用的第一个参数，并将该指针作为访问用户内存的地址，在此例中作为保存sysinfo结构体的缓冲区
  uint64 addr;
  if(argaddr(0, &addr) < 0)   //此时addr为返回用户空间参数的地址
    return -1;

  //计算空闲内存和正在运行的进程数量，存放在结构体sysinfo中
  struct sysinfo sinfo;
  sinfo.freemem = count_freemem();
  sinfo.nproc = count_process();
  printf("%d\n", sinfo.freemem);
  printf("%d\n", sinfo.nproc);
  //使用copyout函数将内核空间数据拷贝到用户空间
  if(copyout(myproc()->pagetable, addr, (char*)&sinfo, sizeof(sinfo)) < 0){
    printf("copy data to user failed\n");
    return -1;
  }
  return 0;
}