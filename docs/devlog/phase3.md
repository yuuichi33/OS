### FCFS

1. **创建时间戳 (`ctime`)**：在进程控制块 `struct proc` 中增加 `ctime` 字段，在进程创建（`allocproc`）时，读取系统全局滴答数 `ticks` 赋值给它。
2. **非抢占式 FCFS**：
   * 在默认的轮转（RR）调度下，每次时钟中断（`which_dev == 2`）都会调用 `yield()` 让出 CPU。
   * 在 FCFS 模式下，在trap中 **禁用时钟中断处的 `yield()`**，使进程能一直独占 CPU 运行，直到它主动退出（`exit`）或阻塞（`sleep`），从而实现非抢占。
3. **动态切换**：引入全局变量 `sched_mode`（`0` 表示 RR，`1` 表示 FCFS），并通过一个系统调用允许用户态动态修改。

