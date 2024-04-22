#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

void run(char* name, char** arg){
    if(fork() == 0){
        exec(name, arg);
        exit(0);
    }
}

int main(int argc, char* argv[]){
    char buf[256];    //从标准输入读取参数的内存池
    char* p = buf, *start_p = buf; //读取时使用到的指针，初始化为指向buf的第一个位置

    char *argbuf[128];     //存放全部参数的数组指针，数组内每个元素指针指向一个参数
    char **args = argbuf;   //对数组指针进行操作的指针，初始化指向第一个指针

    // 存放自身全部参数
    for(int i = 1; i < argc; i++){
        *args = argv[i];
        args++;               //此时args指向自身全部参数的后一个位置，也就是要拼接参数的第一个位置，以防标准输入的参数有换行，args不能一直往后移动，因此新建指针
    }

    char **arg_p = args;
    while (read(0, p, 1) != 0)
    {
        if(*p == ' '){
            *p = '\0';  //替换为字符串结束符
            *arg_p = start_p;
            arg_p++;
            start_p = p + 1;
            
        }
        else if(*p == '\n'){
            *p = '\0';
            *arg_p = start_p;
            start_p = p + 1;
            arg_p = args;
            run(argv[1], argbuf);   //argv[1]是xargs后要执行的命令
        }
        p++;
    }
    //如果文件结尾没有换行符
    if(arg_p != args){
        *p = '\0';
        *arg_p = start_p;
        run(argv[1], argbuf);
    }

    while(wait(0) != -1);
    exit(0);
}