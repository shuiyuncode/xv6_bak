// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
  uint* ref_count;
} kmem;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
    // 计算存放页面引用计数器占用的页数
  uint64 rc_pgsize = (PHYSTOP - (uint64)end) / PGSIZE + 1;
  rc_pgsize = rc_pgsize * sizeof(uint) / PGSIZE + 1;
  // 从[end, end+rc_offset) 存放页引用计数器，需要rc_pages页
  kmem.ref_count = (uint*)end;// 0x80021dd0   *p = p[0]; p是指针
  // 存放计数器的存储空间大小为：
  uint64 rc_offset = rc_pgsize << 12;// rc_offset = 32*4k
  freerange(end + rc_offset, (void*)PHYSTOP);
}

// 将地址转换为物理页号
inline int
pg_index(void* pa)
{ // 第一次计算时  (0x80042000 - 0x80022000) >> 12 = 32
  return ((char*)pa - (char*)PGROUNDUP((uint64)end )) >> 12;
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);// PGROUNDUP(0x8002 1dd0 + 32*4k) = 0x8004 2000
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE){
    // 初始化为1 方便kfree中使用
    kmem.ref_count[pg_index(p)] = 1;
    kfree(p);
  }
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  acquire(&kmem.lock);
  if(--kmem.ref_count[pg_index(pa)] != 0)  // 终极大坑 怎么写了个这个 kmem.ref_count != 0
  {
    release(&kmem.lock);// do not forget this
    return;
  }
  release(&kmem.lock);  

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r){
    kmem.freelist = r->next;
    kmem.ref_count[pg_index((void*)r)] = 1; // equals kmem.rf_count++
  }
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}


void
kaddref(void *pa){
  acquire(&kmem.lock);
  kmem.ref_count[pg_index(pa)]++;
  release(&kmem.lock);
}
