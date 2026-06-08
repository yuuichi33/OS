// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

struct {
  struct spinlock lock;
  char counts[PHYSTOP / PGSIZE]; // 引用计数数组，最大物理内存为 PHYSTOP (224MB)
} page_ref;


void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&page_ref.lock, "pageref"); 
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
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
  
  // 判断引用计数。如果 > 1 则递减并直接返回（不真正释放）
  acquire(&page_ref.lock);
  if(page_ref.counts[(uint64)pa / PGSIZE] > 1) {
    page_ref.counts[(uint64)pa / PGSIZE]--;
    release(&page_ref.lock);
    return;
  }
  page_ref.counts[(uint64)pa / PGSIZE] = 0;
  release(&page_ref.lock);

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
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r){
    // 分配物理页时引用计数为1
    acquire(&page_ref.lock);
    page_ref.counts[(uint64)r / PGSIZE] = 1;
    release(&page_ref.lock);

    memset((char*)r, 5, PGSIZE); // fill with junk
  }
  return (void*)r;
}

// 基于 First-Fit 的内核堆内存分配器 (kmalloc/kmfree)
struct kmem_header {
  uint64 size;              // 内存块大小（不含首部）
  struct kmem_header *next; // 指向下一个块
  int is_free;              // 是否空闲
};

struct {
  struct spinlock lock;
  struct kmem_header *head;
} kmalloc_mem;

// 初始化 kmalloc 自旋锁
void
kmalloc_init(void)
{
  initlock(&kmalloc_mem.lock, "kmalloc");
  kmalloc_mem.head = 0;
}

void*
kmalloc(uint64 size)
{
  if(size == 0)
    return 0;

  // 对齐到 8 字节
  size = (size + 7) & ~7;

  acquire(&kmalloc_mem.lock);

  struct kmem_header *curr = kmalloc_mem.head;

  // 1. 尝试在现有链表中寻找满足大小的空闲块
  while(curr) {
    if(curr->is_free && curr->size >= size) {
      // 如果块足够大，将其拆分
      if(curr->size >= size + sizeof(struct kmem_header) + 8) {
        struct kmem_header *new_block = (struct kmem_header*)((char*)curr + sizeof(struct kmem_header) + size);
        new_block->size = curr->size - size - sizeof(struct kmem_header);
        new_block->is_free = 1;
        new_block->next = curr->next;

        curr->size = size;
        curr->next = new_block;
      }
      curr->is_free = 0;
      release(&kmalloc_mem.lock);
      return (void*)(curr + 1); // 返回首部之后的实际数据地址
    }
    curr = curr->next;
  }

  // 2. 链表中无满足条件的块，调用页分配器申请一个新的 4KB 物理页
  release(&kmalloc_mem.lock);
  void *page = kalloc();
  if(page == 0)
    return 0;

  acquire(&kmalloc_mem.lock);
  struct kmem_header *new_page_block = (struct kmem_header*)page;
  new_page_block->size = PGSIZE - sizeof(struct kmem_header);
  new_page_block->is_free = 1;
  new_page_block->next = 0;

  // 挂载新页到链表头部
  new_page_block->next = kmalloc_mem.head;
  kmalloc_mem.head = new_page_block;

  // 3. 重新对新申请的页空间进行拆分并分配
  curr = new_page_block;
  if(curr->size >= size + sizeof(struct kmem_header) + 8) {
    struct kmem_header *new_block = (struct kmem_header*)((char*)curr + sizeof(struct kmem_header) + size);
    new_block->size = curr->size - size - sizeof(struct kmem_header);
    new_block->is_free = 1;
    new_block->next = curr->next;

    curr->size = size;
    curr->next = new_block;
  }
  curr->is_free = 0;
  release(&kmalloc_mem.lock);
  return (void*)(curr + 1);
}

void
kmfree(void *addr)
{
  if(addr == 0)
    return;

  struct kmem_header *header = (struct kmem_header*)addr - 1;

  acquire(&kmalloc_mem.lock);
  header->is_free = 1;

  // 4. 合并物理上连续的相邻空闲块
  struct kmem_header *curr = kmalloc_mem.head;
  while(curr) {
    if(curr->is_free && curr->next && curr->next->is_free) {
      // 如果当前块末尾刚好是下一个块的起始，说明物理连续
      if((char*)curr + sizeof(struct kmem_header) + curr->size == (char*)curr->next) {
        curr->size += sizeof(struct kmem_header) + curr->next->size;
        curr->next = curr->next->next;
        continue; // 重新检查新的下一个块
      }
    }
    curr = curr->next;
  }
  release(&kmalloc_mem.lock);
}

// 内核态单元测试：验证分配器的正确性与分支覆盖
int
kmalloctest_run(void)
{
  printf("kmalloc test: starting...\n");

  // 1. 测试空指针安全
  kmfree(0);

  // 2. 测试基本分配、写入与 8 字节对齐
  char *p1 = kmalloc(5); 
  if(p1 == 0){
    printf("test failed: p1 is null\n");
    return -1;
  }
  memset(p1, 0xAA, 5); // 确保写入不崩溃

  char *p2 = kmalloc(8);
  if(p2 == 0){
    printf("test failed: p2 is null\n");
    return -1;
  }

  // 3. 测试 First-Fit 地址复用
  uint64 addr_p1 = (uint64)p1;
  kmfree(p1); // 释放 p1
  
  char *p3 = kmalloc(8); // 重新申请，应当复用刚刚释放的 p1 地址
  if((uint64)p3 != addr_p1){
    printf("test failed: failed to reuse freed address\n");
    return -1;
  }

  // 4. 测试相邻空闲块合并
  kmfree(p3);
  kmfree(p2); // 释放相邻的 p2，此时 p3 和 p2 应合并

  // 5. 测试跨页大内存申请 (大于 4KB，触发底层 kalloc)
  char *p4 = kmalloc(5000); 
  if(p4 == 0){
    printf("test failed: p4 (large allocation) is null\n");
    return -1;
  }
  kmfree(p4);

  printf("kmalloc test: all branches passed!\n");
  return 0;
}

// cow fork
// 增加物理页的引用计数
void
ref_inc(uint64 pa)
{
  if(pa < (uint64)end || pa >= PHYSTOP)
    return;
  acquire(&page_ref.lock);
  page_ref.counts[pa / PGSIZE]++;
  release(&page_ref.lock);
}

// 减少并获取物理页引用计数
void
ref_dec(uint64 pa)
{
  if(pa < (uint64)end || pa >= PHYSTOP)
    return;
  acquire(&page_ref.lock);
  page_ref.counts[pa / PGSIZE]--;
  release(&page_ref.lock);
}

// 获取当前的引用计数
int
ref_get(uint64 pa)
{
  if(pa < (uint64)end || pa >= PHYSTOP)
    return 0;
  int c;
  acquire(&page_ref.lock);
  c = page_ref.counts[pa / PGSIZE];
  release(&page_ref.lock);
  return c;
}