### lazy allocation

- 参考：https://pdos.csail.mit.edu/6.S081/2020/labs/lazy.html

- 传统的 sbrk(n)：用户请求增加 n 字节内存。内核立刻调用 kalloc 申请物理页，并通过 mappages 建立虚拟到物理的映射。这在请求很大时非常耗时，且很多程序申请了内存却根本不使用。
- Lazy 延迟分配：
  - 申请时：sbrk 不分配任何物理页，也不修改页表。它仅仅把进程的大小 p->sz 往上累加 n 字节，然后立刻返回。
  - 触发时：当用户程序实际开始读写这片“虚拟”内存时，由于没有建立物理映射，CPU 会瞬间触发 Page Fault（缺页异常）。
- 内核拦截处理：内核在 usertrap() 中拦截到这个异常，检查该异常地址是否在 [0, p->sz) 的合法堆区间内。如果在，内核在此刻才调用 kalloc 申请一页物理内存，并用 mappages 建立映射，然后让用户程序重新执行刚才那条指令。

#### 错误：输入指令导致 Shell 进程静默

系统能正常启动，但输入任何用户命令（如 alltests），子进程直接退回命令提示符 `$`，没有任何报错输出。
<center><img src="figs/fig09.png" width="50%"></center>

- 原因：原先在 usertrap() 拦截缺页时，使用 `if(walkaddr(p->pagetable, va0) == 0)` 来检测目标虚拟地址是否尚未映射。但我们在 vm.c 中为了支持系统调用读写，对 walkaddr 进行重构（当 walkaddr 发现地址未映射但处于合法堆内时，会自动为其分配物理页并建立映射）。导致内核执行 `setkilled(p)`，直接将子进程静默杀死了。
- 解决：在 usertrap 安全检测中，不调用会自动分配内存的 walkaddr，改用 `walk(p->pagetable, va0, 0)` 函数直接去查询页表项（PTE）。walk 只做纯粹的页表项物理检索，不包含任何动态内存分配逻辑。
