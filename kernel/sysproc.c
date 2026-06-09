#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;
  struct proc *p = myproc();

  argint(0, &n);
  addr = p->sz;

  if(n < 0) {
    if(-n > p->sz)
      return -1; 
    // 内存缩减：必须立即调用 growproc 释放物理页
    if(growproc(n) < 0)
      return -1;
  } else {
    // 延迟分配：只向上增长虚拟边界 sz，不分配物理页
    if(p->sz + n >=  MAXVA || p->sz + n < p->sz) // 限制进程大小不能超过 248 GB
      return -1;
    p->sz += n;
  }
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if(n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

struct uproc {
  int pid;
  int state;
  uint64 sz;
  char name[16];
};

uint64
sys_getprocs(void)
{
  int max;
  uint64 uaddr;
  struct proc *p;
  struct uproc kprocs[64]; 
  int count = 0;
  extern struct proc proc[];

  argint(0, &max);
  argaddr(1, &uaddr);

  if(max > 64) max = 64;
  if(max < 0) return -1;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state != UNUSED) {
      if(count < max) {
        kprocs[count].pid = p->pid;
        kprocs[count].state = p->state;
        kprocs[count].sz = p->sz;
        safestrcpy(kprocs[count].name, p->name, sizeof(p->name));
        count++;
      }
    }
    release(&p->lock);
  }

  // 安全检查会在 copyout 中进行
  if(copyout(myproc()->pagetable, uaddr, (char*)kprocs, count * sizeof(struct uproc)) < 0)
    return -1;

  return count; 
}

uint64
sys_kmalloctest(void)
{
  extern int kmalloctest_run(void);
  return kmalloctest_run();
}

uint64
sys_sched_switch(void)
{
  int mode;
  argint(0, &mode);
  if(mode == 0 || mode == 1){
    extern int sched_mode;
    sched_mode = mode;
    return 0;
  }
  return -1;
}

uint64
sys_waitpid(void)
{
  int pid;
  uint64 addr;
  int options;

  argint(0, &pid);
  argaddr(1, &addr);
  argint(2, &options);

  extern int waitpid(int, uint64, int);
  return waitpid(pid, addr, options);
}

uint64
sys_sem_alloc(void)
{
  int init_val;
  argint(0, &init_val);
  extern uint64 sem_alloc(int);
  return sem_alloc(init_val);
}

uint64
sys_sem_free(void)
{
  uint64 sem_addr;
  argaddr(0, &sem_addr);
  extern int sem_free(uint64);
  return sem_free(sem_addr);
}

uint64
sys_sem_wait(void)
{
  uint64 sem_addr;
  argaddr(0, &sem_addr);
  extern int sem_wait(uint64);
  return sem_wait(sem_addr);
}

uint64
sys_sem_signal(void)
{
  uint64 sem_addr;
  argaddr(0, &sem_addr);
  extern int sem_signal(uint64);
  return sem_signal(sem_addr);
}

uint64
sys_sigalarm(void)
{
  int interval;
  uint64 handler;
  struct proc *p = myproc();

  argint(0, &interval);
  argaddr(1, &handler);

  p->alarm_interval = interval;
  p->alarm_handler = handler;
  p->alarm_ticks = 0;

  return 0;
}

uint64
sys_sigreturn(void)
{
  struct proc *p = myproc();
  
  // 恢复之前备份的所有通用寄存器
  *p->trapframe = *p->alarm_tf;
  p->alarm_running = 0;

  // 必须返回恢复后现场的 a0 寄存器值，否则内核系统调用分发框架会用 0 覆盖用户态的 a0，导致 test1 变量损坏。
  return p->trapframe->a0;
}