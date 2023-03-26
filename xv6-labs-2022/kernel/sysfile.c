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

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  argint(n, &fd);
  if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == 0)
    return -1;
  if(pfd)
    *pfd = fd;
  if(pf)
    *pf = f;
  return 0;
}

// fdalloc根据给定的ftable中的打开文件f，在用户进程的打开文件表ofile中记录f，并且分配一个文件描述符。
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

  argaddr(1, &p);
  argint(2, &n);
  if(argfd(0, 0, &f) < 0)
    return -1;
  return fileread(f, p, n);
}

uint64
sys_write(void)
{
  struct file *f;
  int n;
  uint64 p;
  
  argaddr(1, &p);
  argint(2, &n);
  if(argfd(0, 0, &f) < 0)
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

  argaddr(1, &st);
  if(argfd(0, 0, &f) < 0)
    return -1;
  return filestat(f, st);
}

// sys_link为给定的inode创建新的目录条目，即创建新的硬链接。
// 首先sys_link从寄存器获取参数，a0是旧路径名，a1是新路径名。
// 如果旧路径名存在而且不是目录（目录不能创建硬链接），那么就使该inode的硬链接数加1。
// 然后，sys_link调用nameiparent来查找新路径名的最后一个元素的父目录，然后在该目录中，
// 创建一个新的目录条目。限制条件是，新路径名必须存在，
// 而且要与旧路径名位于同一个设备上，因为inode号只在同一个设备上有效。
// Create the path new as a link to the same inode as old.  hard link
uint64
sys_link(void)
{
  char name[DIRSIZ], new[MAXPATH], old[MAXPATH];
  struct inode *dp, *ip;
  // a0是旧路径名，a1是新路径名
  if(argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
    return -1;

  begin_op();
  if((ip = namei(old)) == 0){ 
    end_op(); // 旧路径名不存在
    return -1;
  }

  ilock(ip);
  if(ip->type == T_DIR){// 旧路径名存在而且不是目录（目录不能创建硬链接）
    iunlockput(ip);
    end_op();
    return -1;
  }

  ip->nlink++; // 旧的硬链接数加1
  iupdate(ip);
  iunlock(ip);

  // find the parent directory and final path element of new
  // The new parent directory must exist and be on the same device as the existing inode
  if((dp = nameiparent(new, name)) == 0)  // 查找新路径名的最后一个元素的父目录
    goto bad;
  ilock(dp); // 然后在该目录中，创建一个新的目录条目
  
  // creates a new directory entry pointing at old’s inode
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


// 以下三种情况会用到create
// open with the O_CREATE flag makes a new ordinary file
// mkdir makes a new directory  
// mkdev makes a new device file
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
    if(type == T_SYMLINK && (ip->type == T_SYMLINK || ip->type == T_DEVICE))
      return ip;
    iunlockput(ip);
    return 0;
  }

  if((ip = ialloc(dp->dev, type)) == 0){
    iunlockput(dp);
    return 0;
  }

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if(type == T_DIR){  // Create . and .. entries.
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      goto fail;
  }

  if(dirlink(dp, name, ip->inum) < 0)
    goto fail;

  if(type == T_DIR){
    // now that success is guaranteed:
    dp->nlink++;  // for ".."
    iupdate(dp);
  }

  iunlockput(dp);

  return ip;

 fail:
  // something went wrong. de-allocate ip.
  ip->nlink = 0;
  iupdate(ip);
  iunlockput(ip);
  iunlockput(dp);
  return 0;
}

uint64
sys_open(void)
{
  char path[MAXPATH];
  int fd, omode;
  struct file *f;
  struct inode *ip;
  int n;

  argint(1, &omode);
  if((n = argstr(0, path, MAXPATH)) < 0)
    return -1;

  begin_op();

  if(omode & O_CREATE){
    ip = create(path, T_FILE, 0, 0); // create 内部存在ilock没哟释放
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

  // open a device not a file
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

  // open a file or a device
  if(ip->type == T_DEVICE){
    f->type = FD_DEVICE;
    f->major = ip->major;
  } else if(ip->type == T_SYMLINK){
    // open a symbolic link
    f->type = FD_INODE;
    f->off = 0;
    if(omode & O_NOFOLLOW){ // open the symbolic link self

    } else{ // open the link file, get link filename from data block
      
      end_op(); // 先结束上面的事务，因为要读的文件边变成了 linked file, path 换成其他的文件了
      
      int max_recursion_depth = 10;
      while(max_recursion_depth){
        
        readi(ip, 1, (uint64)(&path), 0, ip->size);// data 区域存的的link filename
        max_recursion_depth--;

        begin_op();
        if((ip = namei(path)) == 0){
          end_op();
          return -1;// make sure the linked file exist
        }
        if(ip->type != T_SYMLINK){
          end_op();
          break;
        }
      }

      if(max_recursion_depth < 0){
        end_op();
        return -1;// the links form a cycle 
      }
    }
  } else{
    f->type = FD_INODE;
    f->off = 0;
  }

  f->ip = ip;                       // O_WRONLY 只写模式
  f->readable = !(omode & O_WRONLY);// 如果打开文件时没有指定只写模式，就把文件的可读属性设为真（true），否则设为假（false）。
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
  argint(1, &major);
  argint(2, &minor);
  if((argstr(0, path, MAXPATH)) < 0 ||
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

  argaddr(1, &uargv);
  if(argstr(0, path, MAXPATH) < 0) {
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

  argaddr(0, &fdarray);
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

// success (0) or failure (-1)      man symlink
// int symlink(const char *target, const char *linkpath);
// symlink() creates a symbolic link named linkpath which contains the string target.
uint64
sys_symlink(void)
{
  char target[MAXPATH];
  char linkpath[MAXPATH];
  int n;
  struct inode* ip;

  if(argstr(0, linkpath, MAXPATH) < 0)
    return -1;
  if((n = argstr(0, target, MAXPATH)) < 0)
    return -1;

  

  // xv6中文件名称存在文件系统的目录中1。
  // 目录是一种特殊的文件，它包含了一系列的条目，每个条目由一个文件名称和一个inode号组成1。
  // inode号是一个唯一的标识符，它指向了文件系统中存储文件内容和元数据的数据结构1。

  // 假设你有一个文件叫做hello.txt，它存储在根目录下。那么根目录就是一个目录文件，它包含了一个条目，
  // 这个条目的文件名称是hello.txt，inode号是1（假设）。inode号1对应了一个inode结构，
  // 它记录了hello.txt文件的大小、类型、权限、数据块地址等信息。数据块地址指向了实际存储hello.txt文件内容的磁盘空间。

  // 使用inode号而不直接用文件名称的原因有以下几点：

  // 1 inode号是唯一的，而文件名称可能重复。例如，不同的目录下可能有同名的文件，或者同一个目录下可能有多个硬链接指向同一个文件。使用inode号可以避免混淆和冲突。
  // 2 inode号是固定长度的，而文件名称可能是变长的。使用inode号可以简化目录结构和查找算法，提高效率和节省空间。
  // 3 inode号可以方便地实现文件系统的抽象层次。例如，xv6中有一种特殊的inode类型叫做设备inode，它对应了一些设备文件（如控制台、磁盘等）。
  //   使用inode号可以让系统以统一的方式处理不同类型的文件。
  begin_op();
  ip = create(linkpath, T_SYMLINK, 0, 0);
  if(ip == 0){
    end_op();
    return -1;
  }

  // write data to file
  // do not need ilock ip because create() has ilock
  if((writei(ip, 0, (uint64)(&target), 0, n)) != n){
    iunlockput(ip);
    end_op();
    return -1;
  }

  iunlockput(ip);
  end_op();
  return 0;
}
