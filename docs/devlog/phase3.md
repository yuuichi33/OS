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
  - 我们定义 WNOHANG = 1。如果传入 options = 1，且目标子进程还在运行，waitpid 不会挂起，而是立即返回 0。这在写一些不需要卡死的父进程时非常实用。

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