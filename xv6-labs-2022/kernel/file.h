// file 面向用户层  inode面向os  dinode面向device
// file.type是一个枚举类型，它定义了文件系统中的文件类型。
//    它有三个可能的值：FD_NONE（未使用的文件描述符），FD_INODE（普通文件或目录），FD_DEVICE（设备文件）。
// inode.type是一个短整型，它定义了内存中的inode结构的类型。
//    它有四个可能的值：T_DIR（目录），T_FILE（普通文件），T_DEV（设备文件），T_UNUSED（未使用的inode）。
// dinode.type是一个短整型，它定义了磁盘上的dinode结构的类型2。
//    它和inode.type有相同的取值范围和含义。

struct file {
  enum { FD_NONE, FD_PIPE, FD_INODE, FD_DEVICE } type;
  int ref; // reference count
  char readable;
  char writable;
  struct pipe *pipe; // FD_PIPE
  struct inode *ip;  // FD_INODE and FD_DEVICE
  uint off;          // FD_INODE
  short major;       // FD_DEVICE
};

#define major(dev)  ((dev) >> 16 & 0xFFFF)
#define minor(dev)  ((dev) & 0xFFFF)
#define	mkdev(m,n)  ((uint)((m)<<16| (n)))

// in-memory copy of an inode
struct inode {
  uint dev;           // Device number
  uint inum;          // Inode number
  int ref;            // Reference count
  struct sleeplock lock; // protects everything below here
  int valid;          // inode has been read from disk?

  short type;         // copy of disk inode
  short major;
  short minor;
  short nlink;
  uint size;
  // addrs[NDIRECT] addrs[NDIRECT+1] 存的是blockno，而要真正读取对应此判断上具体的block number时
  // 要先把addrs[NDIRECT]对应的block(例如addrs[NDIRECT]=1992 ) 加载内存中 对应的内存结构体为buf
  // 再从buf.data上 找到对应的实际物理磁盘的block number
  // 同理读取addrs[NDIRECT+1]也是如此  --> 改为NDIRECT+2更为合理
  uint addrs[NDIRECT+2];
};

// map major device number to device functions.
struct devsw {
  int (*read)(int, uint64, int);
  int (*write)(int, uint64, int);
};

extern struct devsw devsw[];

#define CONSOLE 1
