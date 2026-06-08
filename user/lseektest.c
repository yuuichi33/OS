#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

#define assert(cond, msg) if(!(cond)) { printf("lseektest failed: %s\n", msg); exit(1); }

int
main(int argc, char *argv[])
{
  printf("lseektest: starting...\n");

  // 异常参数分支测试
  assert(lseek(-1, 0, SEEK_SET) < 0, "invalid fd");
  assert(lseek(0, 0, SEEK_SET) < 0, "non-inode fd (stdin)");

  // 创建临时测试文件
  int fd = open("lseek_temp", O_CREATE | O_RDWR);
  assert(fd >= 0, "create file");

  assert(lseek(fd, 0, 99) < 0, "invalid whence");
  assert(lseek(fd, -5, SEEK_SET) < 0, "negative offset");

  // 测试 SEEK_SET 
  write(fd, "abcdefghij", 10); // 写入 10 字节，当前文件大小 size = 10
  int off = lseek(fd, 4, SEEK_SET);
  assert(off == 4, "SEEK_SET to 4");

  char buf[4];
  read(fd, buf, 3); // 应当读取出 "efg"
  assert(memcmp(buf, "efg", 3) == 0, "read after SEEK_SET");

  // 测试 SEEK_CUR
  // 刚刚 read 了 3 字节，当前 off 应为 7。向前相对定位 2 字节（即到 9）
  off = lseek(fd, 2, SEEK_CUR); 
  assert(off == 9, "SEEK_CUR +2");

  // 测试 SEEK_END
  // size 是 10，倒退 3 字节（应定位到 7）
  off = lseek(fd, -3, SEEK_END); 
  assert(off == 7, "SEEK_END -3");

  read(fd, buf, 2); // 应当读取出 "hi"
  assert(memcmp(buf, "hi", 2) == 0, "read after SEEK_END");

  // 清理
  close(fd);
  unlink("lseek_temp");

  printf("lseektest passed!\n");
  exit(0);
}