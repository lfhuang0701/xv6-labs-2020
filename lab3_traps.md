# 1.Trap代码执行流程，以shell执行write系统调用为例

总体框图：

![](https://mit-public-courses-cn-translatio.gitbook.io/~gitbook/image?url=https%3A%2F%2F1977542228-files.gitbook.io%2F%7E%2Ffiles%2Fv0%2Fb%2Fgitbook-legacy-files%2Fo%2Fassets%252F-MHZoT2b_bcLghjAOPsJ%252F-MKFsfImgYCtnwA1d2hO%252F-MKHxleUqYy-y0mrS48w%252Fimage.png%3Falt%3Dmedia%26token%3Dab7c66bc-cf61-4af4-90fd-1fefc96c7b5f&width=768&dpr=4&quality=100&sign=1d837279a6b66bc2b9b89b6bf74ba461029b4c0fc2594301f33a697f6f299061)

系统调用全流程详解：

[[6.S081——陷阱部分(一文读懂xv6系统调用)——xv6源码完全解析系列(5)\_sd ra, 40(a0)-CSDN博客](https://blog.csdn.net/zzy980511/article/details/130255251?ops_request_misc=%257B%2522request%255Fid%2522%253A%2522171444503816800211534199%2522%252C%2522scm%2522%253A%252220140713.130102334.pc%255Fall.%2522%257D&request_id=171444503816800211534199&biz_id=0&utm_medium=distribute.pc_search_result.none-task-blog-2~all~first_rank_ecpm_v1~rank_v31_ecpm-5-130255251-null-null.142^v100^pc_search_result_base8&utm_term=xv86%E7%B3%BB%E7%BB%9F%E8%B0%83%E7%94%A8%E5%85%A8%E6%B5%81%E7%A8%8B&spm=1018.2226.3001.4187)]

# lab4参考文章详解

[6.S081——Lab4——trap lab\_6.s081 lab4-CSDN博客](https://blog.csdn.net/zzy980511/article/details/131069746?ops_request_misc=%257B%2522request%255Fid%2522%253A%2522171498188716800226580603%2522%252C%2522scm%2522%253A%252220140713.130102334..%2522%257D&request_id=171498188716800226580603&biz_id=0&utm_medium=distribute.pc_search_result.none-task-blog-2~all~sobaiduend~default-2-131069746-null-null.142^v100^pc_search_result_base8&utm_term=s081%20lab4&spm=1018.2226.3001.4187)

# lab4.1 RISC-V assembly

## 实验内容

v6仓库中有一个文件***user/call.c***。执行`make fs.img`编译它，并在***user/call.asm***中生成可读的汇编版本。阅读***call.asm***中函数`g`、`f`和`main`的代码。回答下面问题

## 结果

Q: 哪些寄存器存储了函数调用的参数？举个例子，main 调用 printf 的时候，13 被存在了哪个寄存器中？
A: a0-a7; a2;

Q: main 中调用函数 f 对应的汇编代码在哪？对 g 的调用呢？ (提示：编译器有可能会内联(inline)一些函数)
A: 没有这样的代码。 g(x) 被内联到 f(x) 中，然后 f(x) 又被进一步内联到 main() 中

Q: printf 函数所在的地址是？
A: 0x630，也就是ra+1536，注意jalr时的ra为0x30

Q: 在 main 中 jalr 跳转到 printf 之后，ra 的值是什么？
A: 0x38, jalr 指令的下一条汇编指令的地址，jalr指令的地址是0x34,下一条汇编指令的地址就是0x34 + 4 = 0x38 。

Q: 运行下面的代码

unsigned int i = 0x00646c72;
printf("H%x Wo%s", 57616, &i);
输出是什么？
如果 RISC-V 是大端序的，要实现同样的效果，需要将 i 设置为什么？需要将 57616 修改为别的值吗？
A: %x就是将整数以16进制格式化输出，%s是以字符串格式化输出，输出结果是"He110 World"; 需要更改为0x726c6400; 不需要更改57616，因为57616 的十六进制是 110，无论端序（十六进制和内存中的表示不是同个概念）

Q: 在下面的代码中，'y=' 之后会答应什么？ (note: 答案不是一个具体的值) 为什么?

printf("x=%d y=%d", 3);
A: 输出的是一个受调用前的代码影响的“随机”的值。因为 printf 尝试读的参数数量比提供的参数数量多。
第二个参数 `3` 通过 a1 传递，而第三个参数对应的寄存器 a2 在调用前不会被设置为任何具体的值，而是会
包含调用发生前的任何已经在里面的值。首先上述行为是没有定义的行为(Undefined Behavior)，因为printf中传入的参数数量少于格式化字符串中要求的数量，我不打算在此详细介绍前因后果(涉及到va_list等等，而va_list其实是一个指针，指向一块连续的内存区域)，写出来篇幅会很长。我使用GDB调试了上述程序，发现5309其实是紧跟在3之后的一块未初始化的内存数据，这就是问题的答案。

# lab4.2 BackTrace(moderate)

## 实验内容

实现回溯函数backtrace，他能够打印出函数调用栈的地址，从而方便调试，可用于panic函数内，实现发现错误时更好地定位错误

## 知识点

主要熟悉Xv6中函数调用栈帧结构（stack fram），栈帧指针（Fram Pointer），简称FP，指向栈的头部，FP-8指向函数返回地址，FP-16指向上一级函数的栈帧指针FP

由此可以根据当前fp不断向前遍历，直到达到栈底（xv6中内核栈只有一页大小），可用PGROUNDUP宏获取栈底，作为循环结束的条件

## 实现流程

1. 在kernel/printf.c添加backtrace函数，并在defs.h中声明，方便其他函数使用
2. 在sys_sleep函数中调用printf函数，方便测试

## 出现的问题

回溯得到的最后一个返回地址为0x0000000000000012，使用addr2line -e kernel/kernel进行地址翻译时，出现结果??:0，出现错误，原因在于在backtrace中循环终止条件fp不是严格小于栈底 ，这会造成fp等于栈底，而栈底这个值在用户陷入内核态执行系统调用时残存在FP寄存器中的值，它指向一个很小的地址，这个地址本质上指向了一个原先用户态的虚拟地址，并不是系统调用时的地址，解决方法就是修改循环条件，使fp严格小于栈底
