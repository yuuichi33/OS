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

### COW Fork 

- 参考：https://pdos.csail.mit.edu/6.S081/2025/labs/cow.html

- 原生的 fork 极其低效，拷贝父进程所有的物理内存给子进程。
- COW ：在 fork 调用 uvmcopy 时，完全不分配新的物理页。子进程的页表直接指向父进程相同的物理地址。同时，将父进程和子进程中所有可写的页表项全部清除写权限（清除 PTE_W），并打上自定义的写时复制标记 PTE_COW。
- 物理页引用计数（Reference Count）：
  - 由于多个进程的页表同时指向同一个物理页，传统的“进程退出即释放物理页”逻辑将导致系统崩溃。
  - 在 kalloc.c 中引入一个全局数组 page_ref。当物理页被多处共享映射时，引用计数递增；当进程释放映射（kfree）时，引用计数递减。只有当引用计数递减到 0 时，该物理页才会被真正归还给空闲链表。
- 缺页中断分割（Split on Write）：
  - 当父进程或子进程尝试修改打上 PTE_COW 标记的只读页面时，CPU 触发 scause 15（写缺页异常）。
  - 内核拦截该异常，读取该物理页的引用计数：
    - 如果计数为 1：说明其他共享该页的进程已经退出了，当前进程是该页的唯一拥有者。我们直接将该页的 PTE_COW 清除，重新赋予 PTE_W 写权限，无需任何拷贝，原地通过。
    - 如果计数 > 1：说明还有其他进程共享该页。我们调用 kalloc 申请一个新物理页，将原页内容通过 memmove 拷贝过去，在当前进程页表中重新映射并开启 PTE_W 权限，最后将原物理页的引用计数递减（调用 kfree）。

### mmap/munmap 

- 参考：https://pdos.csail.mit.edu/6.S081/2025/labs/mmap.html

- 在传统的物理 I/O 中，用户读写文件必须通过 read/write 系统调用，这涉及到“磁盘 -> 内核缓存 -> 用户缓存”的多次数据拷贝，开销极大。 mmap（Memory Mapping） 采用另一种设计：直接把磁盘上的文件，映射到进程的虚拟地址空间中。
- 核心原理（Lazy File Loading）
  - 当用户调用 mmap(addr, len, prot, flags, fd, offset) 申请文件映射时：
    - 内核同样不分配物理内存，也不读取文件
    - 内核只在进程的 PCB 中，记录一块全新的虚拟内存区域——VMA（Virtual Memory Area，虚拟内存区域）。
    - VMA 记录了这片虚拟地址的起点、长度、读写权限（prot）、映射标志（flags，如共享 MAP_SHARED 或私有 MAP_PRIVATE）以及对应的文件指针 f 和偏移量。
- 缺页装载（On-Demand Paging）
  - 当进程第一次访问这片 VMA 虚拟地址时，CPU 触发缺页异常（Page Fault）。
  - 内核拦截异常，并在 usertrap 中发现异常地址处于某一个 VMA 范围内：
    - 调用 kalloc 申请一个物理页；
    - 从该 VMA 记录的文件中，调用 readi 自动将对应的数据块读取到新分配的物理页中；
    - 用 mappages 将物理页映射到该缺页虚拟地址。
    - 用户程序无缝地继续执行，此时它已经可以直接像读写内存数组一样，读写磁盘文件了。
- 内存回写与释放（munmap）
  - 当用户调用 munmap(addr, len) 时：
    - 如果映射标志是 MAP_SHARED（共享映射）且页面被修改过（Dirty 页），内核必须通过 writei 将该内存页的数据刷回磁盘文件，保证修改不丢失。
    - 然后，通过 uvmunmap 拆除该虚拟地址的页表映射并释放物理页。

- 设计方案
  - VMA 结构体：定义在 proc.h 中，用来追踪每一片映射区域。
  - 虚拟地址自动检索：在 mmap 时，我们从 0x40000000 (1GB) 向上检索未使用的 VMA 边界，为用户程序分配安全的起始地址。
  - Lazy 缺页文件读取：当用户程序首次访问 VMA 范围内的内存时触发缺页。内核拦截该异常，调用 kalloc 申请物理页，并调用 readi 直接将文件内容读取到该物理页 中，最后建立映射。
  - 页面自动写回 (munmap / exit)：如果用户手动调用 munmap 或进程发生 exit 退出，对于被修改过的 MAP_SHARED（共享映射）页，内核通过 writei 自动将物理页中的最新修改刷回磁盘文件。

#### 错误1：fork_test 发生 panic: sched locks 的调试记录

运行 fork_test 时，抛出 `panic: sched locks`。
<center><img src="figs/fig0111.png" width="50%"></center>

- 原因：在 xv6 内核设计中，进程在持有任何 Spinlock 期间，如进程锁 `p->lock` 或等待锁 `wait_lock`，绝对不允许执行任何会引发“睡眠/阻塞”（Sleep）的操作。在原先的进程退出 exit() 设计中，将 VMA 释放和 MAP_SHARED 脏数据写回逻辑放置在 `acquire(&wait_lock)` 语句的下方。
- 解决方法：将 VMA 释放和 MAP_SHARED 放置在 exit() 函数的开头。

#### 错误2：fork_test 发生 mismatch at 2048 磁盘文件损坏的调试记录

在 fork_test 中，子进程在执行 VMA 数据校验时，报错 `mismatch at 2048, wanted 'A', got 0x0`。
<center><img src="figs/fig0112.png" width="50%"></center>

- 原因分析：测试程序通过 `munmap(p1, PGSIZE)` 释放了 2 页 VMA 映射区中的第 1 个页面。内核在 sys_munmap 处理从 VMA 起点开始的部分解映射时，虽然正确向后挪动了虚拟起点（`v->addr += len`）并缩减了长度（`v->len -= len`），但遗漏了更新文件偏移量 `v->offset`。
- 解决方法：增加`v->offset += len;`，使文件偏移量与虚拟起点同步向后挪动。
