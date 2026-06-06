#include "kernel/types.h"
#include "user/user.h"

void assert(int condition, char *msg) {
  if(!condition){
    printf("Assertion failed: %s\n", msg);
    exit(1);
  }
}

int
main(int argc, char *argv[])
{
  printf("semtest: starting rigorous tests...\n");

  // 测试一：无效参数防御分支
  assert(sem_free(0) < 0, "free null pointer");
  assert(sem_wait(0) < 0, "wait null pointer");
  assert(sem_signal(0) < 0, "signal null pointer");
  printf("Test 1: invalid parameter protection passed.\n");

  // 测试二：互斥锁（无需睡眠/无需唤醒）分支
  uint64 mutex = sem_alloc(1); // 初始值为 1
  assert(mutex != 0, "alloc mutex");

  assert(sem_wait(mutex) == 0, "wait mutex should succeed immediately");
  // 此时 count 为 0，再释放时 count 加 1 变为 1 且无需唤醒任何人
  assert(sem_signal(mutex) == 0, "signal mutex should succeed immediately");
  
  sem_free(mutex);
  printf("Test 2: non-blocking P/V branches passed.\n");

  // 测试三：同步（阻塞/唤醒）分支
  uint64 sync_sem = sem_alloc(0); // 初始值为 0
  assert(sync_sem != 0, "alloc sync_sem");

  int pid = fork();
  if(pid < 0) {
    printf("semtest: fork failed\n");
    exit(1);
  }

  if(pid == 0) {
    // 子进程
    printf("Child: working...\n");
    sleep(5);
    printf("Child: finished, signaling parent.\n");
    sem_signal(sync_sem); // 唤醒父进程
    exit(0);
  } else {
    // 父进程
    printf("Parent: waiting on sync_sem...\n");
    sem_wait(sync_sem); // 必然发生阻塞并进入 sleep
    printf("Parent: woke up successfully!\n");
    
    wait(0);
    sem_free(sync_sem);
    printf("Test 3: blocking P/V (sleep/wakeup) branches passed.\n");
  }

  // 测试四：堆内存泄漏（Stress & Recycle）分支
  printf("Test 4: testing kmalloc/kmfree leak prevention...\n");
  for(int i = 0; i < 50; i++) {
    uint64 temp = sem_alloc(1);
    if(temp == 0) {
      printf("Test 4 failed: leaked memory at iteration %d\n", i);
      exit(1);
    }
    sem_free(temp); // 必须被 kmfree 回收，否则 50 次循环一定会耗尽内核堆
  }
  printf("Test 4: memory recycle branch passed.\n");

  printf("semtest: all strict branches passed successfully!\n");
  exit(0);
}