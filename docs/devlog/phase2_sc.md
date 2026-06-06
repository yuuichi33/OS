### 异常防护机制

---

1. 进程状态与标记（在 `proc.h` 中）
`struct proc` 是进程控制块（PCB）。其中有两个关键字段：
- **`enum procstate state`**：进程状态（包括 `UNUSED`（空闲）、`RUNNING`（运行中）、`ZOMBIE`（僵尸态）等）。
- **`int killed`**：这是一个“被杀”标记。如果非 `0`，说明该进程已经被标记为待清理。

2. 用户态异常入口（在 `trap.c` 的 `usertrap` 中）
当用户程序运行发生任何中断或异常（包括崩溃）时，硬件会跳转到 `usertrap(void)` 函数。
当前它通过读取 `r_scause()`（中断/异常原因寄存器）进行三路分流：

```c
if(r_scause() == 8){
    // 1. 系统调用 (System Call)
    ...
} else if((which_dev = devintr()) != 0){
    // 2. 硬件中断 (如时钟中断、键盘中断等)
    // ok
} else {
    // 3. 发生用户态异常（程序崩溃、非法指令、内存越界等）
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    setkilled(p); // 标记杀死该进程
}
```

- **现在的做法**：如果程序崩溃（比如执行了非法指令或访问了越界内存），xv6 只会打印一行含糊的 `unexpected scause`，然后调用 `setkilled(p)`。
- **安全性保证**：在 `usertrap` 的末尾，有这样一行代码：
  ```c
  if(killed(p))
    exit(-1);
  ```
  确保了只要进程被标记为 `killed`，它就会在离开内核前调用 `exit(-1)` 退出并回收资源。用户程序崩溃，而 xv6 内核依然不会 Panic。

---

根据 RISC-V 架构的 `scause` 定义，拦截并识别特定的用户态非法操作，打印错误诊断信息（如 `Segmentation fault` 段错误），然后安全回收资源。

-  RISC-V 常见异常代码（`scause`）：
    - **`2`**：Illegal instruction（非法指令，比如用户程序尝试执行一条损坏的二进制指令）
    - **`12`**：Instruction page fault（指令缺页/非法执行，尝试执行无权限内存中的代码）
    - **`13`**：Load page fault（读内存段错误，尝试读取无权限或未映射的地址）
    - **`15`**：Store/AMO page fault（写内存段错误，尝试向只读或未映射的地址写入数据，比如写空指针）

### 动态内存分配器

基于 xv6 现有的物理页分配器（kalloc/kfree），在其之上构建一个内核级字节级动态内存分配器 kmalloc 与 kmfree。

- 采用首部链表法（First-Fit Header-based Allocator）：
- 每次申请，内核会寻找足够大的空闲块。如果空闲块过大，则进行拆分。
- 如果没有空闲块，则调用页分配器 kalloc() 申请一个全新的 4KB 物理页，将其格式化为一个大空闲块并挂载到链表上。
- 释放时，将块标记为空闲，并自动合并相邻的空闲内存块以防碎片化。
- 整个过程通过一个自旋锁保护，确保多核安全。