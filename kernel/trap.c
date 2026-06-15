#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "fs.h"         
#include "sleeplock.h"  
#include "file.h"       
#include "fcntl.h"   

// FCFS 模式下防饿死：每个 CPU 每 8 个 tick 让出一次 CPU
static uint64 fcfs_noyield[NCPU];

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
void
usertrap(void)
{
  int which_dev = 0;

  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();
  
  // save user program counter.
  p->trapframe->epc = r_sepc();
  
  if(r_scause() == 8){
    // system call

    if(killed(p))
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->trapframe->epc += 4;

    // an interrupt will change sepc, scause, and sstatus,
    // so enable only now that we're done with those registers.
    intr_on();

    syscall();
  } else if((which_dev = devintr()) != 0){
    // ok
    // alarm
    if(which_dev == 2 && p->alarm_interval > 0 && p->alarm_running == 0) {
      p->alarm_ticks++;
      if(p->alarm_ticks == p->alarm_interval) {
        p->alarm_ticks = 0;
        p->alarm_running = 1;
        // 备份当前所有的用户寄存器现场
        *p->alarm_tf = *p->trapframe;
        // 重定向用户程序返回地址至 handler
        p->trapframe->epc = p->alarm_handler;
      }
    }
  } else {
    // 细化拦截并处理用户态异常，确保内核稳定
    uint64 scause = r_scause();
    uint64 stval = r_stval(); // 发生异常的地址
    uint64 sepc = r_sepc();   // 发生异常的指令地址
    // 处理用户态触发的按需缺页异常
    if(scause == 13 || scause == 15) {
      if(stval >= MAXVA) {
        setkilled(p);
      } else {
        uint64 va0 = PGROUNDDOWN(stval);
        // pte_t *pte = walk(p->pagetable, va0, 0);
        // 增加 VMA 检查
        // 1. 先检查该异常地址是否属于某一个 VMA 映射区
        struct vma *v = 0;
        for(int i = 0; i < 16; i++) {
          if(p->vmas[i].valid && stval >= p->vmas[i].addr && stval < p->vmas[i].addr + p->vmas[i].len) {
            v = &p->vmas[i];
            break;
          }
        }

        if(v != 0) {
          // 异常分支防御：如果发生了写异常，但该映射区是只读的，报错 Kill 进程
          if(scause == 15 && !(v->prot & PROT_WRITE)) {
            setkilled(p);
          } else {
            // 进行 VMA 物理页按需读取与分配
            char *mem = kalloc();
            if(mem == 0) {
              setkilled(p);
            } else {
              memset(mem, 0, PGSIZE);
              
              // 锁住 Inode，调用 readi 从文件的指定位置读取 4096 字节到物理内存中
              ilock(v->f->ip);
              int file_offset = v->offset + (va0 - v->addr);
              readi(v->f->ip, 0, (uint64)mem, file_offset, PGSIZE);
              iunlock(v->f->ip);

              // 计算映射标志权限
              int perm = PTE_U;
              if(v->prot & PROT_READ) perm |= PTE_R;
              if(v->prot & PROT_WRITE) perm |= PTE_W;

              if(mappages(p->pagetable, va0, PGSIZE, (uint64)mem, perm) < 0) {
                kfree(mem);
                setkilled(p);
              }
            }
          }
        } else {
          // 2. 如果不属于 VMA，再走原来的 Lazy / COW 页面处理逻辑
          pte_t *pte = walk(p->pagetable, va0, 0);
          if(pte == 0 || (*pte & PTE_V) == 0) {
          if(stval < PGROUNDUP(p->sz)) {
              char *mem = kalloc();
              if(mem == 0) {
                // 物理内存耗尽，Kill 进程
                setkilled(p);
              } else {
                memset(mem, 0, PGSIZE);
                if(mappages(p->pagetable, va0, PGSIZE, (uint64)mem, PTE_R|PTE_W|PTE_U) < 0) {
                  kfree(mem);
                  setkilled(p);
                }
              }
            } else {
              // 只读写保护异常，Kill
              setkilled(p);
            }
        } else if((*pte & PTE_COW) && scause == 15) {
            // COW 触发的写中断，执行物理页分裂
            if(cow_alloc(p->pagetable, va0) < 0) {
              setkilled(p);
            }
          } else {
            // 只读写保护异常，Kill
            setkilled(p);
          }
        }
      }
    } else {
      switch (scause) {
        case 2: // 非法指令 (Illegal Instruction)
          printf("\n[Kernel Exception] Process %d (%s) killed due to: Illegal Instruction\n", p->pid, p->name);
          printf("                   at PC: %p, Instruction: %p\n", sepc, stval);
          break;
        case 13: // 读段错误 (Load Page Fault / Segmentation fault)
          printf("\n[Kernel Exception] Process %d (%s) killed due to: Segmentation Fault (Invalid Read)\n", p->pid, p->name);
          printf("                   at PC: %p, Accessing Address: %p\n", sepc, stval);
          break;
        case 15: // 写段错误 (Store Page Fault / Segmentation fault)
          printf("\n[Kernel Exception] Process %d (%s) killed due to: Segmentation Fault (Invalid Write)\n", p->pid, p->name);
          printf("                   at PC: %p, Accessing Address: %p\n", sepc, stval);
          break;
        case 12: // 执行段错误 (Instruction Page Fault)
          printf("\n[Kernel Exception] Process %d (%s) killed due to: Instruction Page Fault (Execution Denied)\n", p->pid, p->name);
          printf("                   at PC: %p\n", sepc);
          break;
        default: // 其他未细化的异常
          printf("\n[Kernel Exception] Process %d (%s) killed due to: Unknown Exception (scause %p)\n", p->pid, p->name, scause);
          printf("                   at PC: %p, stval: %p\n", sepc, stval);
          break;
      }
    
      // 标记进程被杀死，退出码为 -1
      setkilled(p);
    }
  }
  if(killed(p))
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  extern int sched_mode; // FCFS NP
  if(which_dev == 2) {
    if(sched_mode == 0) {
      yield();
    } else {
      // FCFS: 每 8 个 tick 让出一次，防止多核飢餓死锁
      int id = cpuid();
      fcfs_noyield[id]++;
      if(fcfs_noyield[id] >= 8) {
        fcfs_noyield[id] = 0;
        yield();
      }
    }
  }

  usertrapret();
}

//
// return to user space
//
void
usertrapret(void)
{
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  intr_off();

  // send syscalls, interrupts, and exceptions to uservec in trampoline.S
  uint64 trampoline_uservec = TRAMPOLINE + (uservec - trampoline);
  w_stvec(trampoline_uservec);

  // set up trapframe values that uservec will need when
  // the process next traps into the kernel.
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  
  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  w_sepc(p->trapframe->epc);

  // tell trampoline.S the user page table to switch to.
  uint64 satp = MAKE_SATP(p->pagetable);

  // jump to userret in trampoline.S at the top of memory, which 
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64))trampoline_userret)(satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){
    printf("scause %p\n", scause);
    printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // give up the CPU if this is a timer interrupt.
  extern int sched_mode; // FCFS NP
  if(which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING) {
    if(sched_mode == 0) {
      yield();
    } else {
      // FCFS: 每 8 个 tick 让出一次，防止多核飢餓死锁
      int id = cpuid();
      fcfs_noyield[id]++;
      if(fcfs_noyield[id] >= 8) {
        fcfs_noyield[id] = 0;
        yield();
      }
    }
  }

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void
clockintr()
{
  acquire(&tickslock);
  ticks++;
  wakeup(&ticks);
  release(&tickslock);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int
devintr()
{
  uint64 scause = r_scause();

  if((scause & 0x8000000000000000L) &&
     (scause & 0xff) == 9){
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();

    if(irq == UART0_IRQ){
      uartintr();
    } else if(irq == VIRTIO0_IRQ){
      virtio_disk_intr();
    } else if(irq){
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if(irq)
      plic_complete(irq);

    return 1;
  } else if(scause == 0x8000000000000001L){
    // software interrupt from a machine-mode timer interrupt,
    // forwarded by timervec in kernelvec.S.

    if(cpuid() == 0){
      clockintr();
    }
    
    // acknowledge the software interrupt by clearing
    // the SSIP bit in sip.
    w_sip(r_sip() & ~2);

    return 2;
  } else {
    return 0;
  }
}

