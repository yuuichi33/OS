#include "kernel/types.h"
#include "user/user.h"

void
compute(int id)
{
  for(int i = 0; i <= 40; i++) {
    // 密集计算循环，消耗 CPU 滴答
    for(volatile int j = 0; j < 2000000; j++) {}
    if(i % 10 == 0) {
      printf("Process %d compute progress: %d%%\n", id, i * 2 + 20);
    }
  }
  printf("Process %d finished.\n", id);
  exit(0);
}

int
main(int argc, char *argv[])
{
  int n = 3;
  printf("Starting schedtest (spawning %d compute processes)...\n", n);

  for(int i = 0; i < n; i++) {
    int pid = fork();
    if(pid < 0) {
      printf("schedtest: fork failed\n");
      exit(1);
    }
    if(pid == 0) {
      compute(i + 1); // 子进程
    }
    sleep(1); // 确保子进程创建时间戳 (ctime) 产生微小先后差异
  }

  // 回收子进程
  for(int i = 0; i < n; i++) {
    wait(0);
  }

  printf("schedtest: finished.\n");
  exit(0);
}