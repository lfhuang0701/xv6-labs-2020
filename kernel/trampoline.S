	#
        # code to switch between user and kernel space.
        #
        # this code is mapped at the same virtual address
        # (TRAMPOLINE) in user and kernel space so that
        # it continues to work when it switches page tables.
	#
	# kernel.ld causes this to be aligned
        # to a page boundary.
        #
	.section trampsec
.globl trampoline
trampoline:
.align 4
.globl uservec
uservec:    
	#
        # trap.c sets stvec to point here, so
        # traps from user space start here,
        # in supervisor mode, but with a
        # user page table.
        #
        # sscratch points to where the process's p->trapframe is
        # mapped into user space, at TRAPFRAME.
        # 译：trap.c设置了stvec(程序计数寄存器)指向这里，所以源于用户态的trap从这里开始
        # 当前CPU处于supervisor模式，但页表还是用户页表
        # sscratch指向的是进程的p->trapframe在内存中的映射位置，也就是TRAPFRAME
        
	# swap a0 and sscratch
        # so that a0 is TRAPFRAME
        #交换前a0寄存器是传来的第一个参数，sscratch是trapframe（在返回用户态时设置，之后会提到）
        #交换后a0是trapframe，sscratch是传来的第一个参数
        csrrw a0, sscratch, a0

        # save the user registers in TRAPFRAME
        sd ra, 40(a0)
        sd sp, 48(a0)
        sd gp, 56(a0)
        sd tp, 64(a0)
        sd t0, 72(a0)
        sd t1, 80(a0)
        sd t2, 88(a0)
        sd s0, 96(a0)
        sd s1, 104(a0)
        sd a1, 120(a0)
        sd a2, 128(a0)
        sd a3, 136(a0)
        sd a4, 144(a0)
        sd a5, 152(a0)
        sd a6, 160(a0)
        sd a7, 168(a0)
        sd s2, 176(a0)
        sd s3, 184(a0)
        sd s4, 192(a0)
        sd s5, 200(a0)
        sd s6, 208(a0)
        sd s7, 216(a0)
        sd s8, 224(a0)
        sd s9, 232(a0)
        sd s10, 240(a0)
        sd s11, 248(a0)
        sd t3, 256(a0)
        sd t4, 264(a0)
        sd t5, 272(a0)
        sd t6, 280(a0)

	# save the user a0 in p->trapframe->a0
        csrr t0, sscratch            //先将aacratch的值读取到t0，此时sscratch保留的是之前a0的值，也就是传来的第一个参数
        sd t0, 112(a0)                //将t0值存放到trapfram->a0中，这里的trapfram->a0与实际用户寄存器a0不同


        #下面设置与内核相关的配置，这些配置在p->trapframe中，是在内核执行usertrapret时添加到p->trapfram的

        # restore kernel stack pointer from p->trapframe->kernel_sp
        ld sp, 8(a0)

        # make tp hold the current hartid, from p->trapframe->kernel_hartid
        ld tp, 32(a0)

        # load the address of usertrap(), p->trapframe->kernel_trap
        ld t0, 16(a0)

        # restore kernel page table from p->trapframe->kernel_satp
        ld t1, 0(a0)
        csrw satp, t1

     @ 现在这里输出的是由内核设置好的巨大的kernel page table。所以现在我们成功的切换了page table，
     @ 我们在这个位置进展的很好，Stack Pointer指向了kernel stack；我们有了kernel page table，可以读取kernel data。
     @ 我们已经准备好了执行内核中的C代码了。这里还有个问题，为什么代码没有崩溃？毕竟我们在内存中的某个位置执行代码，
     @ 程序计数器保存的是虚拟地址，如果我们切换了page table，为什么同一个虚拟地址不会通过新的page table寻址走到一些无关的page中？
     @ 看起来我们现在没有崩溃并且还在执行这些指令。有人来猜一下原因吗？
     @ 学生回答：因为我们还在trampoline.S代码中，而trampoline代码在用户空间和内核空间都映射到了同一个地址。
     @ 完全正确。我不知道你们是否还记得user page table的内容，trampoline page在user page table中的映射与kernel page table中的映射是完全一样的。这两个page table中其他所有的映射都是不同的，只有trampoline page的映射是一样的，因此我们在切换page table时，寻址的结果不会改变，我们实际上就可以继续在同一个代码序列中执行程序而不崩溃。这是trampoline page的特殊之处，它同时在user page table和kernel page table都有相同的映射关系。
     @ 之所以叫trampoline page，是因为你某种程度在它上面“弹跳”了一下，然后从用户空间走到了内核空间。

        #清空快表TLB
        sfence.vma zero, zero

        # a0 is no longer valid, since the kernel page
        # table does not specially map p->tf.
        # 译：a0此时不再有效，因为内核地址空间并没有用户的trapframe的映射

        # jump to usertrap(), which does not return
        jr t0

        #我们通过trampoline.S中的uservec函数成功实现了上下文的保存和用户态到内核态的切换，
        #现在我们看到的地址空间完全是内核地址空间，而且使用的程序栈也是内核栈。


.globl userret
userret:
        # userret(TRAPFRAME, pagetable)
        # switch from kernel to user.
        # usertrapret() calls here.
        # a0: TRAPFRAME, in user page table.
        # a1: user page table, for satp.
        # 译：调用形式userret(TRAMPFRAME, pagetable)
        # 负责从内核态到用户态切换
        # usertrapret函数调用这里
	# a0：TRAPFRAME的值，这对应用户页表中trapframe的位置
	# a1：用户页表对应的satp寄存器的值
                        

        # switch to the user page table.
        # 将页表切换为用户页表
        # <至此，整个地址空间完全切换到了用户地址空间>
        # 由于trampoline.S在用户和内核地址空间中映射的地址一样
        # 所以代码的执行得以无缝衔接，这和uservec的逻辑是一样的
        csrw satp, a1
        sfence.vma zero, zero

        # put the saved user a0 in sscratch, so we
        # can swap it with our a0 (TRAPFRAME) in the last step.
        # 译：将被保存的用户的a0(系统调用返回值)放置到sscratch中，所以在最后一步我们可以将它
        # 与我们的a0进行互换，这样就可以成功实现a0的恢复和sscratch指向TRAPFRAME
        # 这里112(a0)指的就是p->trapfram->a0,而用户寄存器a0是trapfram
        ld t0, 112(a0)
        csrw sscratch, t0

        # restore all but a0 from TRAPFRAME
        # 译：从TRAPFRAME中恢复除了a0以外的所有寄存器
        ld ra, 40(a0)
        ld sp, 48(a0)
        ld gp, 56(a0)
        ld tp, 64(a0)
        ld t0, 72(a0)
        ld t1, 80(a0)
        ld t2, 88(a0)
        ld s0, 96(a0)
        ld s1, 104(a0)
        ld a1, 120(a0)
        ld a2, 128(a0)
        ld a3, 136(a0)
        ld a4, 144(a0)
        ld a5, 152(a0)
        ld a6, 160(a0)
        ld a7, 168(a0)
        ld s2, 176(a0)
        ld s3, 184(a0)
        ld s4, 192(a0)
        ld s5, 200(a0)
        ld s6, 208(a0)
        ld s7, 216(a0)
        ld s8, 224(a0)
        ld s9, 232(a0)
        ld s10, 240(a0)
        ld s11, 248(a0)
        ld t3, 256(a0)
        ld t4, 264(a0)
        ld t5, 272(a0)
        ld t6, 280(a0)

	# restore user a0, and save TRAPFRAME in sscratch
        #交换a0和sscratch，这样交换后用户寄存器a0就是调用的返回值，sscratch就是trapframe（下次系统调用会用到）
        csrrw a0, sscratch, a0
        
        # return to user mode and user pc.
        # usertrapret() set up sstatus and sepc.
        # 译：返回用户模式和，恢复对应的PC值
        # usertrapret()已经提前设置好了sstatus和sepc的值
        # 故sret可以正常执行，这里的sret指令与ecall指令都有硬件完成，作用正好相反
        sret
