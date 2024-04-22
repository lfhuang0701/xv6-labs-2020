# lab1 xv6 and Unix utilities

## sleep（easy）

## pingpong（easy）

## primes（moderate）

1. 使用fork和pipe筛选素数
2. 使用pipe向子进程传递数据，子进程进行判断后继续通过pipe向子进程传递数据，直到判断完毕

## find（moderate）

1. 参考ls的实现，使用递归对目录进行查询，读取到文件时进行比对。
2. 涉及结构体stat（文件或目录详细信息），可使用stat或fstat函数获取，结构体dirent（目录条目信息，inod及name），read（）获取
3. 使用指针向目录信息后添加新内容，mmove（）函数

## xargs（moderate）

1. 实现了类似readling功能，即从标准输入读取一行数据
2. 参数的读取用到了较多指针及数组，实现方法为定义数组，使用指针修改数组内容，
3. 使用指针的读取字符串参数，传递时只需添加字符串结束符再穿出首地址即可

# lab2 systemcall

##
