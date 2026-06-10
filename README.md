# xv6-riscv 增强型操作系统内核

[进度汇报](docs/process.md) `docs/process.md`

## quick start

### 1. 编译并启动内核
在 Ubuntu 环境下，使用以下命令编译并启动 QEMU 模拟器：
```bash
make qemu
```

### 2. 运行自动化集成测试
系统成功引导并进入 Shell 后，运行 `alltests` 即可启动自动化验证：
```bash
$ alltests
```
alltests 包括 增量功能单元测试 和 `usertests`。
### 3. 并发压力测试验证
在控制台输入以下命令运行官方的高并发、死锁与竞争条件压力测试：
```bash
$ grind
```
该程序会长时间高并发执行 `fork`、`kill`、文件读写和内存分配，字符交替输出（如 `ABBABA`）代表内核多核同步锁设计正确，运行稳定。