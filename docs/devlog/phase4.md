### lseek 
- 操作系统在 struct file 中使用 off 字段记录当前文件的读写位置（偏移量）。默认的 read 和 write 会自动递增这个值。
- lseek(fd, offset, whence) 系统调用的目的，就是强行修改这个 off 偏移量，从而实现文件任意位置的随机读写。
- whence 参数控制流设计：
  - SEEK_SET (0)：新偏移量设为 offset（绝对定位）。
  - SEEK_CUR (1)：新偏移量设为 当前 off + offset（相对当前位置定位）。
  - SEEK_END (2)：新偏移量设为 文件大小 size + offset（相对文件末尾定位）。需要通过 ilock(ip) 锁住索引节点，以安全读取 ip->size。
- 异常边界防御：
  - 必须拦截非法文件描述符（fd）。
  - 必须拦截非正规文件（管道 FD_PIPE、控制台 FD_DEVICE 均不支持 lseek，应返回 -1）。
  - 必须拦截非法的 whence 参数。
  - 必须拦截计算后小于 0 的非法偏移量（偏移量不允许为负数，返回 -1）。

### Symbolic Link
- 一个特殊类型的文件（类型标记为 T_SYMLINK）。
- 数据块（Data Blocks）中存储的是另一个文件的目标路径名（Target Path）。
- 动态递归解析算法：
  - 当用户调用 open("path", flags) 且未指定 O_NOFOLLOW 标志时：
    - 如果该文件类型是 T_SYMLINK，内核需要读取其数据块内容（获取目标路径 target）。
    - 解锁并释放当前软链接节点，调用 namei(target) 寻找到下一个节点。
- 死循环防御（环路检测）：如果软链接形成环路（如 A -> B -> A），会导致无限递归。我们设置一个计数器 depth，一旦解析深度超过 10 层，判定为环路死锁，立即返回 -1 报错。