// On-disk file system format.
// Both the kernel and user programs use this header file.


#define ROOTINO  1   // root i-number
#define BSIZE 1024  // block size

// Disk layout:
// [ boot block | super block | log | inode blocks |
//                                          free bit map | data blocks]
//
// mkfs computes the super block and builds an initial file system. The
// super block describes the disk layout:
struct superblock {
  uint magic;        // Must be FSMAGIC
  uint size;         // Size of file system image (blocks)
  uint nblocks;      // Number of data blocks
  uint ninodes;      // Number of inodes.
  uint nlog;         // Number of log blocks
  uint logstart;     // Block number of first log block
  uint inodestart;   // Block number of first inode block
  uint bmapstart;    // Block number of first free map block
};

#define FSMAGIC 0x10203040

// #define NDIRECT 12

// The first 11 elements of ip->addrs[] should be direct blocks; 
// the 12th should be a singly-indirect block (just like the current one); 
// the 13th should be your new doubly-indirect block.
// 11 direct blocks + 1 singly-indirect block + 1 doubly-indirect block
// 256*256+256+11 blocks 注意细节呀 不是 256*256+11 !!!
// 这里的12 应该改为11 后面的 uint addrs[NDIRECT+1] 改为 uint addrs[NDIRECT+2]更为合理
#define NDIRECT 11
#define NINDIRECT (BSIZE / sizeof(uint))
#define MAXFILE (NDIRECT + NINDIRECT + NINDIRECT*NINDIRECT)

// dinode是磁盘的存储形式 inode是内存的存储形式
// On-disk inode structure 64byte = 8 + 4 + 52
struct dinode {
  short type;           // File type
  short major;          // Major device number (T_DEVICE only)
  short minor;          // Minor device number (T_DEVICE only)
  short nlink;          // Number of links to inode in file system
  uint size;            // Size of file (bytes)
  uint addrs[NDIRECT+2];   // Data block addresses   11 direct blocks + 1 singly-indirect block + 1 doubly-indirect block
};

// Inodes per block.
#define IPB           (BSIZE / sizeof(struct dinode))

// Block containing inode i
#define IBLOCK(i, sb)     ((i) / IPB + sb.inodestart)

// Bitmap bits per block
#define BPB           (BSIZE*8)

// Block of free map containing bit for block b
#define BBLOCK(b, sb) ((b)/BPB + sb.bmapstart)

// Directory is a file containing a sequence of dirent structures.
#define DIRSIZ 14

struct dirent {
  ushort inum;
  char name[DIRSIZ];
};

