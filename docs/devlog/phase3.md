### FCFS

1. **创建时间戳 (`ctime`)**：在进程控制块 `struct proc` 中增加 `ctime` 字段，在进程创建（`allocproc`）时，读取系统全局滴答数 `ticks` 赋值给它。
2. **非抢占式 FCFS**：
   - 在默认的轮转（RR）调度下，每次时钟中断（`which_dev == 2`）都会调用 `yield()` 让出 CPU。
   - 在 FCFS 模式下，在trap中 **禁用时钟中断处的 `yield()`**，使进程能一直独占 CPU 运行，直到它主动退出（`exit`）或阻塞（`sleep`），从而实现非抢占。
3. **动态切换**：引入全局变量 `sched_mode`（`0` 表示 RR，`1` 表示 FCFS），并通过一个系统调用允许用户态动态修改。

### waitpid

- 精准等待：如果 pid > 0，则只等待并回收 PID 刚好等于 pid 的那个子进程；如果 pid == -1，则表现为等待任意子进程（兼容普通 wait）。
- 非阻塞支持 (WNOHANG)：
  - 在普通 wait 中，如果没有子进程退出，父进程会一直 sleep 挂起。
  - 定义 WNOHANG = 1。如果传入 options = 1，且目标子进程还在运行，waitpid 不会挂起，而是立即返回 0。

### 信号量

- 信号量（Semaphore）的机制与原理
  - 同步机制，本质上是一个受保护的整型变量 count，配合一个自旋锁 lock，用来控制多个进程对共享资源的访问。
  - 两个核心操作（P 操作 和 V 操作）
    - P 操作（Wait / Down，申请资源）
      - 原理：将信号量的值 count 减 1。
      - 如果减 1 后 count >= 0，说明还有空闲资源，进程继续执行。
      - 如果减 1 后 count < 0，说明资源已耗尽，进程必须挂起等待（进入睡眠状态）。
    - V 操作（Signal / Up，释放资源）
      - 原理：将信号量的值 count 加 1。
      - 如果加 1 后 count <= 0，说明有进程正在因为资源耗尽而睡眠，此时内核需要唤醒一个正在睡眠的进程。
  - 与 xv6 的 sleep / wakeup 结合
    - sleep(void *chan, struct spinlock *lk)：让当前进程在通道 chan 上睡眠。
    - wakeup(void *chan)：唤醒所有在通道 chan 上睡眠的进程。
    - 可以直接把信号量结构体本身的内存地址 s 作为睡眠通道 chan

  - P 操作中：如果资源不够，调用 sleep(s, &s->lock)。
  - V 操作中：释放资源后，调用 wakeup(s) 唤醒在该地址上睡眠的进程。

- 方案设计
  - 利用已实现的 kmalloc/kmfree 动态地管理信号量
    - 结构体定义：
      ```code C
      struct sem {
        struct spinlock lock;
        int count;
      };
      ```
    - 动态分配 (sem_alloc)：直接调用 kmalloc(sizeof(struct sem)) 从内核堆中动态申请一个结构体空间，初始化自旋锁和计数器后，直接将它的内核指针地址作为 Handle 返回给用户态。
    - 动态释放 (sem_free)：用户态将指针地址传回，内核将其强转回 struct sem* 并调用 kmfree() 直接释放，将内存归还给堆。
    - P/V 操作 (sem_wait / sem_signal)：直接使用该信号量自身的内存地址作为 sleep / wakeup 的睡眠通道，实现动态阻塞和唤醒。

### alarm
- 参考：https://pdos.csail.mit.edu/6.S081/2025/labs/traps.html

sigalarm 异步报警机制工作原理

sigalarm 机制在内核中本质上是一种用户态异步信号中断与恢复。其核心控制流转换原理分为以下五个阶段：

- 时钟中断触发：用户程序在用户态正常运行时，硬件时钟中断将其强制陷入内核态 usertrap()。内核检测到时钟中断（which_dev == 2）后，对当前进程的累计滴答数（alarm_ticks）进行累加。
- 现场暂存（存档）：当累计滴答数达到用户设定阈值，且当前未处于报警处理状态时，内核利用已实现的 kmalloc 分配空间，将当前进程的 trapframe 现场（包含所有通用寄存器、程序计数器 epc 等）完整备份至 alarm_tf 中。
- 控制流重定向（跳转）：内核将进程当前 trapframe->epc 强行修改为用户注册的警报处理函数（handler）的虚拟地址。当中断返回（usertrapret）至用户态时，CPU 强行跳转去执行报警逻辑。
- 防嵌套重入（隔离）：引入 alarm_running 状态标志。在警报处理函数运行期间，屏蔽后续时钟中断的二次警报触发，防止发生嵌套中断导致调用栈溢出或寄存器覆盖。
- 现场恢复（读档）：警报函数执行完毕后，用户态主动发起 sigreturn 系统调用。内核将备份的 alarm_tf 现场完整写回当前 trapframe，并复位 alarm_running。系统调用返回后，用户程序在原被打断处无缝恢复执行。