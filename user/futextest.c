#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define PGSIZE 4096
#define STACK_SIZE PGSIZE

// 用户态极简 Mutex 结构体
typedef struct {
  volatile int val;
} mutex_t;

volatile int shared_resource = 0;
mutex_t test_mutex = {0};

// 用户态原子加锁（利用 RISC-V 硬件原子交换指令）
void
mutex_lock(mutex_t *m)
{
  // 尝试将 1 交换入 m->val。若原值为 0，说明获取锁成功
  while(__sync_lock_test_and_set(&m->val, 1) != 0) {
    // 竞争失败，调用 futex 进入睡眠。期望当前值为 1 时才睡眠
    int res = futex((void*)&m->val, FUTEX_WAIT, 1);
    // 若返回 -2 说明在进入系统调用前锁已被释放，直接重新尝试
    if(res == -2) continue; 
  }
}

// 用户态原子解锁
void
mutex_unlock(mutex_t *m)
{
  m->val = 0;
  __sync_synchronize();
  // 唤醒最多 1 个在当前锁物理地址上等待的线程
  futex((void*)&m->val, FUTEX_WAKE, 1);
}

// 线程入口：高强度互斥写操作
void
thread_work(void *arg)
{
  for(int i = 0; i < 500; i++) {
    mutex_lock(&test_mutex);
    shared_resource++; // 临界区
    mutex_unlock(&test_mutex);
  }
  exit(0);
}

void
test_branch_a_b()
{
  printf("Testing Branch A & B: Fastpath & Lost Wakeup Prevention...\n");

  mutex_t lock = {0};
  
  // 无竞争加锁，应该直接在用户态成功，无需调用内核
  mutex_lock(&lock);
  if(lock.val != 1) {
    printf("FAIL: Fastpath failed to set lock value\n");
    exit(1);
  }
  mutex_unlock(&lock);
  printf("  Fastpath check: OK.\n");

  // 模拟 Lost Wakeup 竞态。
  // 期望值为 0，但当前实数值为 1。FUTEX_WAIT 应该立即安全返回 -2
  lock.val = 1;
  int res = futex((void*)&lock.val, FUTEX_WAIT, 0);
  if(res != -2) {
    printf("FAIL: Lost wakeup prevention failed, got status: %d\n", res);
    exit(1);
  }
  printf("  Lost Wakeup check: OK.\n");
  printf("Branch A & B PASSED.\n\n");
}

void
test_slowpath_concurrency()
{
  printf("Testing Branch C & D: Slowpath Synchronization & Multicore Stress...\n");

  #define THREAD_NUM 4
  void *stacks[THREAD_NUM];
  
  shared_resource = 0;
  test_mutex.val = 0;

  for(int i = 0; i < THREAD_NUM; i++) {
    stacks[i] = malloc(STACK_SIZE);
    uint64 stop = (uint64)stacks[i] + STACK_SIZE;
    stop &= ~0xF;
    
    int tid = clone(thread_work, (void*)stop, 0);
    if(tid < 0) {
      printf("FAIL: clone thread failed in futex stress\n");
      exit(1);
    }
  }

  for(int i = 0; i < THREAD_NUM; i++) {
    wait(0);
    free(stacks[i]);
  }

  // 若没有同步互斥保护，多核并发写会导致值远远小于 2000
  int expected = THREAD_NUM * 500;
  if(shared_resource != expected) {
    printf("FAIL: Concurrency race detected! Expected %d, Got: %d\n", expected, shared_resource);
    exit(1);
  }

  printf("  Verification: Shared resource value is exactly: %d\n", shared_resource);
  printf("Branch C & D PASSED.\n\n");
}

int
main(int argc, char *argv[])
{
  printf("STARTING FUTEX SYSTEM CALL TESTS...\n\n");

  test_branch_a_b();
  test_slowpath_concurrency();

  printf("ALL FUTEX TESTS PASSED SUCCESSFULLY...\n");
  exit(0);
}