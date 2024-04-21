#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

// 根据输入的路径格式化输出最后一个"/"之后的文件或者目录名字
char* my_fmtname(char* path){
    char* p = path + strlen(path);
    while (*p != '/')
    {
        p--;
    }
    return ++p;
}

void find(char* path, char* filename){
    int fd;
    struct stat st;
    struct dirent de;
    char buf[512], *p;
    if((fd = open(path, 0)) < 0){
        fprintf(2, "open %s error\n", path);
        return; //此处不要exit推出程序，因为有的文件拒绝打开，会打开失败，但仍需要继续find
    }

    if((fstat(fd, &st)) < 0){
        fprintf(2, "stat %s error\n", path);
        close(fd);
        return;
    }

    switch (st.type)
    {
    case T_FILE:
        /* code */
        if(strcmp(my_fmtname(path), filename) == 0){
            printf("%s\n", path);
        }
        break;
    case T_DIR:
        /* code */
        /*
        不能这样写在path后面直接添加内容，因为path没有分配空间，这样写不安全，故需要定义一个buf
        p = path + strlen(path);
        *p++ = '/';
        */
        if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
            printf("find: path too long\n");
            break;
        }
        strcpy(buf, path);
        p = buf + strlen(buf);
        *p++ = '/';
        while(read(fd, &de, sizeof(de)) == sizeof(de)){
            if(de.inum == 0)
                continue;
            if(!strcmp(de.name, ".") || !strcmp(de.name, ".."))
                continue;    
            memmove(p, de.name, DIRSIZ);
            p[DIRSIZ] = 0; //使用DIRSIZ而不是strlen（de.name）的原因是，若下次while循环de.name长度小于这次，那么这次的de.name在下次仍会有保留,添加空字符使其成为一个合法字符串
            find(buf, filename);
        }
        break;
    default:
        break;
    }
    close(fd);


}

int main(int argc, char* argv[]){
    if (argc != 3){
        printf("usage: %s <dir> <filename>", argv[0]);
        exit(0);
    }
    find(argv[1], argv[2]);
    exit(0);
}