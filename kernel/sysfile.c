//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"
#include "memlayout.h"

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  if(argint(n, &fd) < 0)
    return -1;
  if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == 0)
    return -1;
  if(pfd)
    *pfd = fd;
  if(pf)
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int
fdalloc(struct file *f)
{
  int fd;
  struct proc *p = myproc();

  for(fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd] == 0){
      p->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

uint64
sys_dup(void)
{
  struct file *f;
  int fd;

  if(argfd(0, 0, &f) < 0)
    return -1;
  if((fd=fdalloc(f)) < 0)
    return -1;
  filedup(f);
  return fd;
}

uint64
sys_read(void)
{
  struct file *f;
  int n;
  uint64 p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;
  return fileread(f, p, n);
}

uint64
sys_write(void)
{
  struct file *f;
  int n;
  uint64 p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;

  return filewrite(f, p, n);
}

uint64
sys_close(void)
{
  int fd;
  struct file *f;

  if(argfd(0, &fd, &f) < 0)
    return -1;
  myproc()->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

uint64
sys_fstat(void)
{
  struct file *f;
  uint64 st; // user pointer to struct stat

  if(argfd(0, 0, &f) < 0 || argaddr(1, &st) < 0)
    return -1;
  return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
uint64
sys_link(void)
{
  char name[DIRSIZ], new[MAXPATH], old[MAXPATH];
  struct inode *dp, *ip;

  if(argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
    return -1;

  begin_op();
  if((ip = namei(old)) == 0){
    end_op();
    return -1;
  }

  ilock(ip);
  if(ip->type == T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }

  ip->nlink++;
  iupdate(ip);
  iunlock(ip);

  if((dp = nameiparent(new, name)) == 0)
    goto bad;
  ilock(dp);
  if(dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0){
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  iput(ip);

  end_op();

  return 0;

bad:
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  end_op();
  return -1;
}

// Is the directory dp empty except for "." and ".." ?
static int
isdirempty(struct inode *dp)
{
  int off;
  struct dirent de;

  for(off=2*sizeof(de); off<dp->size; off+=sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if(de.inum != 0)
      return 0;
  }
  return 1;
}

uint64
sys_unlink(void)
{
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ], path[MAXPATH];
  uint off;

  if(argstr(0, path, MAXPATH) < 0)
    return -1;

  begin_op();
  if((dp = nameiparent(path, name)) == 0){
    end_op();
    return -1;
  }

  ilock(dp);

  // Cannot unlink "." or "..".
  if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad;

  if((ip = dirlookup(dp, name, &off)) == 0)
    goto bad;
  ilock(ip);

  if(ip->nlink < 1)
    panic("unlink: nlink < 1");
  if(ip->type == T_DIR && !isdirempty(ip)){
    iunlockput(ip);
    goto bad;
  }

  memset(&de, 0, sizeof(de));
  if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");
  if(ip->type == T_DIR){
    dp->nlink--;
    iupdate(dp);
  }
  iunlockput(dp);

  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);

  end_op();

  return 0;

bad:
  iunlockput(dp);
  end_op();
  return -1;
}

static struct inode*
create(char *path, short type, short major, short minor)
{
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if((dp = nameiparent(path, name)) == 0)
    return 0;

  ilock(dp);

  if((ip = dirlookup(dp, name, 0)) != 0){
    iunlockput(dp);
    ilock(ip);
    if(type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE))
      return ip;
    iunlockput(ip);
    return 0;
  }

  if((ip = ialloc(dp->dev, type)) == 0)
    panic("create: ialloc");

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if(type == T_DIR){  // Create . and .. entries.
    dp->nlink++;  // for ".."
    iupdate(dp);
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      panic("create dots");
  }

  if(dirlink(dp, name, ip->inum) < 0)
    panic("create: dirlink");

  iunlockput(dp);

  return ip;
}

uint64
sys_open(void)
{
  char path[MAXPATH];
  int fd, omode;
  struct file *f;
  struct inode *ip;
  int n;

  if((n = argstr(0, path, MAXPATH)) < 0 || argint(1, &omode) < 0)
    return -1;

  begin_op();

  if(omode & O_CREATE){
    ip = create(path, T_FILE, 0, 0);
    if(ip == 0){
      end_op();
      return -1;
    }
  } else {
    if((ip = namei(path)) == 0){
      end_op();
      return -1;
    }
    ilock(ip);
    if(ip->type == T_DIR && omode != O_RDONLY){
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  if(ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)){
    iunlockput(ip);
    end_op();
    return -1;
  }

  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }

  if(ip->type == T_DEVICE){
    f->type = FD_DEVICE;
    f->major = ip->major;
  } else {
    f->type = FD_INODE;
    f->off = 0;
  }
  f->ip = ip;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  if((omode & O_TRUNC) && ip->type == T_FILE){
    itrunc(ip);
  }

  iunlock(ip);
  end_op();

  return fd;
}

uint64
sys_mkdir(void)
{
  char path[MAXPATH];
  struct inode *ip;

  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_mknod(void)
{
  struct inode *ip;
  char path[MAXPATH];
  int major, minor;

  begin_op();
  if((argstr(0, path, MAXPATH)) < 0 ||
     argint(1, &major) < 0 ||
     argint(2, &minor) < 0 ||
     (ip = create(path, T_DEVICE, major, minor)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_chdir(void)
{
  char path[MAXPATH];
  struct inode *ip;
  struct proc *p = myproc();
  
  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);
  if(ip->type != T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  iput(p->cwd);
  end_op();
  p->cwd = ip;
  return 0;
}

uint64
sys_exec(void)
{
  char path[MAXPATH], *argv[MAXARG];
  int i;
  uint64 uargv, uarg;

  if(argstr(0, path, MAXPATH) < 0 || argaddr(1, &uargv) < 0){
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for(i=0;; i++){
    if(i >= NELEM(argv)){
      goto bad;
    }
    if(fetchaddr(uargv+sizeof(uint64)*i, (uint64*)&uarg) < 0){
      goto bad;
    }
    if(uarg == 0){
      argv[i] = 0;
      break;
    }
    argv[i] = kalloc();
    if(argv[i] == 0)
      goto bad;
    if(fetchstr(uarg, argv[i], PGSIZE) < 0)
      goto bad;
  }

  int ret = exec(path, argv);

  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);

  return ret;

 bad:
  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);
  return -1;
}

uint64
sys_pipe(void)
{
  uint64 fdarray; // user pointer to array of two integers
  struct file *rf, *wf;
  int fd0, fd1;
  struct proc *p = myproc();

  if(argaddr(0, &fdarray) < 0)
    return -1;
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
    if(fd0 >= 0)
      p->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  if(copyout(p->pagetable, fdarray, (char*)&fd0, sizeof(fd0)) < 0 ||
     copyout(p->pagetable, fdarray+sizeof(fd0), (char *)&fd1, sizeof(fd1)) < 0){
    p->ofile[fd0] = 0;
    p->ofile[fd1] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  return 0;
}

//roster new add
//在进程的地址空间映射文件 
uint64
sys_mmap(void){
  uint64 addr, sz, offset;
  int prot, flags, fd;
  struct file *f;

  //读取参数
  if(argaddr(0, &addr) < 0 || argaddr(1, &sz) < 0 || argint(2, &prot) < 0 || argint(3, &flags) < 0
      || argfd(4, &fd, &f) < 0 || argaddr(5, &offset) < 0){
        return -1;
      }
  //printf("f->readable:%s, f->writable: %s\n", f->readable, f->writable);
  //检查传入的权限与文件的权限是否冲突
  if((!f->readable && (prot & PROT_READ)) || (f->writable && (prot & PROT_WRITE) && (flags & MAP_PRIVATE))
      || (!f->writable && (prot & PROT_WRITE) && (flags & MAP_SHARED))){
    
    //printf("prot fail\n");
    return -1;
  }
  
  //遍历当前进程的vma数组，寻找空闲vma，同时更新vaend，vaend是新的vma的结束地址，用于生长新的vma时确定vastart地址
  struct proc *p = myproc();
  struct vma *v = 0;
  uint64 vaend = MMAPEND;

  for(int i = 0; i < NVMA; i++){
    struct vma *cur_v = &p->vma[i];
    if(cur_v->valid == 0){
      if(v == 0){ //防止重新分配，这里不break循环是因为需要遍历完vma数组进行更新vaend
        v = cur_v;
        v->valid = 1;
      }
    }
    else if(cur_v->vastart < vaend){
      vaend = PGROUNDDOWN(cur_v->vastart);
    }
  }

  if(v == 0){
    panic("no free vma\n");
  }

  sz = PGROUNDUP(sz); //进行页面对齐
  v->vastart = vaend - sz;
  v->f = f;
  v->flags = flags;
  v->offset = offset;
  v->prot = prot;
  v->sz = sz;
  
  filedup(v->f); //增加文件的引用
  return v->vastart; //返回映射的首地址

}

//取消进程的地址空间对于文件的映射
uint64
sys_munmap(void){
  uint64 va, length;
  if(argaddr(0, &va) < 0 || argaddr(1, &length) < 0){
    return -1;
  }
  //printf("va:%p, length:%p\n", va, length);
  struct proc *p = myproc();
  struct vma *v= findvma(va, p);
  //printf("v->vastart:%p, v->sz: %p, v->offset: %p\n", v->vastart, v->sz, v->offset);
  //取消映射的部分只能位于vma的起始部分或结尾部分，位于中间部分（空洞）应该返回错误
  if(va > v->vastart && va + length < v->vastart + v->sz){
    return -1;
  }

  uint64 va_aligned = va;
  if(va > v->vastart){ 
    va_aligned = PGROUNDUP(va);  //起始地址向上页面对齐，若va处于页面中间位置，则说明页面前半部分不能取消映射，此时应该向上对齐，表示此页面不取消映射
  }

  //va页面对齐后要重新计算写入的字节数
  uint64 nunmap_bytes = length - (va_aligned - va);
  if(nunmap_bytes < 0)
    nunmap_bytes = 0;

  //由于要进行写回磁盘操作，故仿造uvmunmap函数自定义取消映射函数vmaunmap（）
  vmaunmap(p->pagetable, va_aligned, nunmap_bytes, v);

  //如果取消映射的是vma的起始部分，应该更新vma的起始地址及文件偏移量,此处应该先更新offset，再更新vastart
  if(va == v->vastart){
    v->offset += length; 
    v->vastart += length;  
  }
  v->sz -= length;

  //printf("v->vastart:%p, v->sz: %p, v->offset: %p\n", v->vastart, v->sz, v->offset);
  if(v->sz <= 0){
    fileclose(v->f);
    v->valid = 0;
    //printf("close v\n");
  }
  //printf("v->vastart:%p, v->sz: %p, v->offset: %p\n", v->vastart, v->sz, v->offset);
  return 0;
}

//定义懒分配函数，用于page fault时进行分配
int
vmalazyalloc(uint64 va, struct proc *p){

  struct vma *v = findvma(va, p);
  //printf("va:%p\n", va);
  if(v == 0){
    printf("find vma fail\n");
    return -1;
  }

  //分配物理页
  void *pa = kalloc();
  memset(pa, 0, PGSIZE);

  //从磁盘读取文件内容到物理页上
  begin_op();
  ilock(v->f->ip);
  //偏移量应该是vma在文件内的偏移量加上当前虚拟地址在vma内的偏移量，在vma内的偏移量需要页面对齐
  readi(v->f->ip, 0, (uint64)pa, PGROUNDDOWN(va - v->vastart) + v->offset, PGSIZE);
  iunlock(v->f->ip);
  end_op();

  //设置pte条目的标志位
  int PTE_flags = PTE_U;
  if(v->prot & PROT_READ){
    PTE_flags |= PTE_R;
  }
  if(v->prot & PROT_WRITE){
    PTE_flags |= PTE_W;
  }
  if(v->prot & PROT_EXEC){
    PTE_flags |= PTE_X;
  }

  //使用mappage映射页面
  if(mappages(p->pagetable, va, PGSIZE, (uint64)pa, PTE_flags) < 0){
    panic("vmalazyalloc: mappages fail");
  }

  return 0;

}

//给定虚拟地址va，查找对应vma指针
struct vma *findvma(uint64 va, struct proc *p){
  for(int i = 0; i < NVMA; i++){
    struct vma *v = &p->vma[i];
    //printf("v%d :v->sz: %p, v->valid:%d, v->vastart: %p, va:%p\n", i, v->sz, v->valid, v->vastart, va);
    if(va >= v->vastart && va < v->vastart + v->sz){
      return v;
    }
  }
  return 0;
}