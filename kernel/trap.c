#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
void
usertrap(void)
{
  // 用于接收处理设备中断返回值的变量
  int which_dev = 0;

  // 读取sstatus寄存器，判断是否来自user mode
  // 如果不是，则陷入panic
  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  // 译：将当前模式下的中断和异常处理全部发送到kernelvec函数
  // 因为我们当前处于内核态下
  // 将stvec寄存器设置为kernelvec，当发生内核陷阱时会跳转到kernelvec而非之前的uservec
  // stvec寄存器保存着发生trap时跳转的位置
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();
  
  // save user program counter.
  // 将程序计数器（SEPC）的值保存一份到trapframe中去，
  //事实上，ecall指令已经将ecall指令的地址保存到SEPC中，用于再次返回用户态时继续往下执行
  // 这里再次保存一份到进程的trapframe中是因为发生这种情况:当程序还在内核中执行时，我们可能切换到另一个进程，并进入到那个程序的用户空间，
  //然后那个进程可能再调用一个系统调用进而导致SEPC寄存器的内容被覆盖。
  //所以，我们需要保存当前进程的SEPC寄存器到一个与该进程关联的内存中，这样这个数据才不会被覆盖。
  p->trapframe->epc = r_sepc();
  
  // 读取scause寄存器
  // 它是在我们调用ecall指令时由指令自动设置的
  // RISC-V标准定义，当scause的值为8时，表示陷阱原因是系统调用
  if(r_scause() == 8){
    // system call

    // 如果进程已经被杀死，那么直接退出
    if(p->killed)
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    //我们并不想再次返回到用户态时还是ecall指令，而是下一条指令
    p->trapframe->epc += 4;

    // an interrupt will change sstatus &c registers,
    // so don't enable until done with those registers.
    //打开中断
    //ecall指令会关闭中断，因为中断可能会改变sstatus和其他寄存器的值，而我们需要使用到这些寄存器
    //现在我们已经使用完毕这些寄存器，因此需要打开中断
    intr_on();

    //执行系统调用
    syscall();
  } else if((which_dev = devintr()) != 0){ // 如果处理的是设备中断(interrupt)，则调用devintr来处理
    // ok
  } 
  // 否则是异常(exception)，杀死当前进程并报错
  else {
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
  }

  //如果进程被杀死，不执行后续步骤，直接结束进程
  if(p->killed)
    exit(-1);

  //timer interrupt.
  if(which_dev == 2 && p->AlarmInterval != 0 && p->InHandle == 0){ //产生时钟中断后，判断警报是否开启，是否在处理警报过程中

      p->TrickCount++;  //计时器加1

      if(p->TrickCount == p->AlarmInterval){  //时间间隔期满

    
          memmove(p->alarm_trapframe, p->trapframe, sizeof(struct trapframe));//保存当前陷阱帧，用于处理警报后在sigreturn恢复现场
          
          p->trapframe->epc = (uint64)(p->alarm_handle); //将内核态进入到用户态的地址设置为警报处理函数的地址

          p->InHandle = 1;  //设置标志位，防止程序重入
      }
    }
  usertrapret(); 
}

//
// return to user space
// 1.关中断(直到执行SRET之前，始终不响应中断)
// 2.设置stvec到uservec
// 3.设置trapframe中与内核有关的信息，为下一次陷阱处理做准备
// 4.正确地设置sepc，跳转回正确的地址继续执行
// 5.调用userret函数，准备恢复现场、切换页表、设置sscratch
//
void
usertrapret(void)
{
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  //关闭中断
  //关闭中断的原因是，在usertrap中，我们更改了STVEC，发生trap时，会跳转到内核中处理trap的位置，
  //下一步我们要更改STVEC寄存器，使发生trap时跳转到用户态处理trap的位置，
  //但是我们还处于内核中，如果打开中断，并且发生中断，我们会跳转到用户态，这会导致内核出错
  intr_off();


  // send syscalls, interrupts, and exceptions to trampoline.S
  // 设置stvec寄存器，将stvec设置为uservec
  // 这是我们之前问题的答案，stvec寄存器就是在这里被设置指向uservec的
  // 你可能会说，这是一个鸡生蛋和蛋生鸡的问题，只有进程触发陷阱才会被设置stvec
  // 事实上，在fork一个新的进程时，因为fork是一个系统调用，那么它在返回到用户态时一定会经过这里
  // 从而在一开始成功设置了stvec寄存器
  w_stvec(TRAMPOLINE + (uservec - trampoline));


  // set up trapframe values that uservec will need when
  // the process next re-enters the kernel.
  // 译：设置trapframe中的值，这些值在下次uservec再次进入内核时会使用到
  // 分别设置：内核页表、内核栈指针、usertrap地址、CPU的ID号
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  // set S Previous Privilege mode to User.
  // 译：设置好trampoline.S中的sret指令会用到的寄存器，以返回用户态
  // 将前一个模式设置为用户态
  // 这里在为返回用户态做准备
  unsigned long x = r_sstatus();

  // 将SPP位清空，确保陷阱返回到用户模式
  // 设置SPIE为1，在返回时SPIE会被设置给SIE，确保supervisor模式下的中断被打开
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  // 将trapframe中保存的pc值写入sepc
  // 注意对于系统调用来说，这里其实指向了陷阱指令的下一条指令
  // 因为在usertrap中我们给trapframe中的epc加4了
  w_sepc(p->trapframe->epc);

  // tell trampoline.S the user page table to switch to.
  // 准备切换回用户页表，准备好要写入的SATP值
  uint64 satp = MAKE_SATP(p->pagetable);

  // jump to trampoline.S at the top of memory, which 
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  // 译：跳转到位于内存顶端的trampoline.S，这将会切换到用户页表
  // 恢复用户寄存器，并使用sret指令回到用户模式
  // 首先得到userret代码的虚拟地址，然后将虚拟地址直接作为函数指针调用之
  // 传入了两个参数TRAPFRAME和satp，按照calling convention，这两个参数将会被放置到a0和a1
  uint64 fn = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64,uint64))fn)(TRAPFRAME, satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){
    printf("scause %p\n", scause);
    printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING)
    yield();

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void
clockintr()
{
  acquire(&tickslock);
  ticks++;
  wakeup(&ticks);
  release(&tickslock);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int
devintr()
{
  uint64 scause = r_scause();

  if((scause & 0x8000000000000000L) &&
     (scause & 0xff) == 9){
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();

    if(irq == UART0_IRQ){
      uartintr();
    } else if(irq == VIRTIO0_IRQ){
      virtio_disk_intr();
    } else if(irq){
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if(irq)
      plic_complete(irq);

    return 1;
  } else if(scause == 0x8000000000000001L){
    // software interrupt from a machine-mode timer interrupt,
    // forwarded by timervec in kernelvec.S.

    if(cpuid() == 0){
      clockintr();
    }
    
    // acknowledge the software interrupt by clearing
    // the SSIP bit in sip.
    w_sip(r_sip() & ~2);

    return 2;
  } else {
    return 0;
  }
}

