#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  printf("waitpidtest: starting...\n");

  // 测试一：非阻塞等待 (WNOHANG)
  int pid1 = fork();
  if(pid1 < 0) {
    printf("waitpidtest: fork 1 failed\n");
    exit(1);
  }
  if(pid1 == 0) {
    sleep(5); // 子进程 1 睡眠 5 
    exit(12); // 退出状态码为 12
  }

  int status;
  // 此时子进程 1 正在睡眠，传入 WNOHANG 应当立即返回 0 且不阻塞
  int ret = waitpid(pid1, &status, WNOHANG);
  if(ret != 0) {
    printf("waitpidtest failed: WNOHANG did not return 0 immediately (got %d)\n", ret);
    exit(1);
  }

  // 测试二：精准等待特定 PID
  int pid2 = fork();
  if(pid2 < 0) {
    printf("waitpidtest: fork 2 failed\n");
    exit(1);
  }
  if(pid2 == 0) {
    exit(34); // 子进程 2 立即退出
  }

  // 稍微延迟，确保子进程 2 已经变成 ZOMBIE，而子进程 1 仍在睡眠
  sleep(1);

  // 我们显式等待仍在睡眠的子进程 1（阻塞式），应当等到它醒来退出并回收它
  ret = waitpid(pid1, &status, 0);
  if(ret != pid1) {
    printf("waitpidtest failed: expected to reap child 1 (%d), got %d\n", pid1, ret);
    exit(1);
  }
  if(status != 12) {
    printf("waitpidtest failed: expected child 1 status 12, got %d\n", status);
    exit(1);
  }

  // 回收已经变成僵尸态很久的子进程 2
  ret = waitpid(pid2, &status, 0);
  if(ret != pid2) {
    printf("waitpidtest failed: expected to reap child 2 (%d), got %d\n", pid2, ret);
    exit(1);
  }
  if(status != 34) {
    printf("waitpidtest failed: expected child 2 status 34, got %d\n", status);
    exit(1);
  }

  // 测试三：通配等待 (pid = -1)
  int pid3 = fork();
  if(pid3 == 0) {
    exit(56);
  }
  // 传入 -1 应当等价于普通 wait，回收任意进程
  ret = waitpid(-1, &status, 0);
  if(ret != pid3) {
    printf("waitpidtest failed: expected to reap any child (pid3: %d), got %d\n", pid3, ret);
    exit(1);
  }
  if(status != 56) {
    printf("waitpidtest failed: expected child 3 status 56, got %d\n", status);
    exit(1);
  }

  printf("waitpidtest: all tests passed!\n");
  exit(0);
}