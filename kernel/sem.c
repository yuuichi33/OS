#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct sem {
  struct spinlock lock;
  int count;
};

// 动态分配信号量
uint64
sem_alloc(int init_val)
{
  // 使用 kmalloc 动态申请内存
  struct sem *s = (struct sem*)kmalloc(sizeof(struct sem));
  if(s == 0)
    return 0;

  initlock(&s->lock, "semaphore");
  s->count = init_val;
  
  // 将结构体指针直接作为句柄返回给用户态
  return (uint64)s;
}

// 动态释放信号量
int
sem_free(uint64 sem_addr)
{
  if(sem_addr == 0)
    return -1;

  // 使用 kmfree 释放内存归还给内核堆
  kmfree((void*)sem_addr);
  return 0;
}

// P 操作 (Wait)
int
sem_wait(uint64 sem_addr)
{
  struct sem *s = (struct sem*)sem_addr;
  if(s == 0)
    return -1;

  acquire(&s->lock);
  s->count--;
  // 资源不足时，在信号量自身的内存地址上 sleep
  while(s->count < 0){
    sleep(s, &s->lock);
  }
  release(&s->lock);
  return 0;
}

// V 操作 (Signal)
int
sem_signal(uint64 sem_addr)
{
  struct sem *s = (struct sem*)sem_addr;
  if(s == 0)
    return -1;

  acquire(&s->lock);
  s->count++;
  // 有进程在等待时，唤醒在信号量地址上睡眠的进程
  if(s->count <= 0){
    wakeup(s);
  }
  release(&s->lock);
  return 0;
}