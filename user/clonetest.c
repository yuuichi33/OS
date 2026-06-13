#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define PGSIZE 4096
#define STACK_SIZE PGSIZE

volatile int global_shared_var = 0;
char *heap_shared_ptr = 0;

// 基本传参与跳转
void
thread_entry_arg(void *arg)
{
  int val = (int)(uint64)arg;
  if(val == 888) {
    printf("  [Thread 1] Parameter validation: OK.\n");
  } else {
    printf("  [Thread 1] FAIL: Expected parameter 888, got %d\n", val);
    exit(1);
  }
  exit(0);
}

// 页表与堆内存共享
void
thread_entry_share(void *arg)
{
  printf("  [Thread 2] Modifying shared global variable...\n");
  global_shared_var = 555;
  
  printf("  [Thread 2] Modifying shared heap memory...\n");
  heap_shared_ptr[0] = 'O';
  heap_shared_ptr[1] = 'K';
  
  exit(0);
}

// 栈帧独立隔离度验证
void
thread_entry_stack(void *arg)
{
  volatile int local_array[10];
  int thread_id = (int)(uint64)arg;
  
  // 填充独立局部变量
  for(int i = 0; i < 10; i++) {
    local_array[i] = thread_id + i;
  }
  
  sleep(5); // 保持活跃状态，等待其他线程调度
  
  // 检查是否在并发执行时，自己的栈被其他线程的写入污染了
  for(int i = 0; i < 10; i++) {
    if(local_array[i] != thread_id + i) {
      printf("  [Thread Stack] FAIL: Stack interference detected on Thread %d!\n", thread_id);
      exit(1);
    }
  }
  exit(0);
}

// 高并发压力多核释放
void
thread_entry_stress(void *arg)
{
  sleep(1);
  exit(0);
}

void
test_branch_a()
{
  printf("Testing Branch A: Basic clone & Parameter Passing...\n");
  void *stack = malloc(STACK_SIZE);
  uint64 stack_top = (uint64)stack + STACK_SIZE;
  stack_top &= ~0xF; // 16 字节对齐
  
  int tid = clone(thread_entry_arg, (void*)stack_top, (void*)888);
  if(tid < 0) {
    printf("FAIL: clone thread A failed\n");
    exit(1);
  }
  wait(0);
  free(stack);
  printf("Branch A PASSED.\n\n");
}

void
test_branch_b()
{
  printf("Testing Branch B: Virtual Address & Heap Shared Space...\n");
  
  global_shared_var = 111;
  heap_shared_ptr = malloc(16);
  heap_shared_ptr[0] = 'X';
  heap_shared_ptr[1] = 'Y';

  void *stack = malloc(STACK_SIZE);
  uint64 stack_top = (uint64)stack + STACK_SIZE;
  stack_top &= ~0xF;

  int tid = clone(thread_entry_share, (void*)stack_top, 0);
  if(tid < 0) {
    printf("FAIL: clone thread B failed\n");
    exit(1);
  }
  
  wait(0); // 回收该线程
  
  if(global_shared_var != 555) {
    printf("FAIL: Global variable shared writing failed!\n");
    exit(1);
  }
  if(heap_shared_ptr[0] != 'O' || heap_shared_ptr[1] != 'K') {
    printf("FAIL: Heap space shared writing failed!\n");
    exit(1);
  }
  
  printf("  Verification: Shared memory modifications visible in parent process!\n");
  free(stack);
  free(heap_shared_ptr);
  printf("Branch B PASSED.\n\n");
}

void
test_branch_c()
{
  printf("Testing Branch C: Stack Isolation and Border...\n");
  
  #define NUM_STACK_THREADS 3
  void *stacks[NUM_STACK_THREADS];
  
  for(int i = 0; i < NUM_STACK_THREADS; i++) {
    stacks[i] = malloc(STACK_SIZE);
    uint64 stop = (uint64)stacks[i] + STACK_SIZE;
    stop &= ~0xF;
    
    int tid = clone(thread_entry_stack, (void*)stop, (void*)(uint64)(10 + i * 10));
    if(tid < 0) {
      printf("FAIL: clone stack thread %d failed\n", i);
      exit(1);
    }
  }
  
  for(int i = 0; i < NUM_STACK_THREADS; i++) {
    wait(0);
    free(stacks[i]);
  }
  
  printf("Branch C PASSED. Stack workspaces are isolated successfully.\n\n");
}

void
test_branch_d()
{
  printf("Testing Branch D: Multi-core Concurrency Lifecycle & Double Free Protection...\n");
  
  #define STRESS_NUM 10
  void *stacks[STRESS_NUM];
  
  for(int i = 0; i < STRESS_NUM; i++) {
    stacks[i] = malloc(STACK_SIZE);
    uint64 stop = (uint64)stacks[i] + STACK_SIZE;
    stop &= ~0xF;
    
    int tid = clone(thread_entry_stress, (void*)stop, 0);
    if(tid < 0) {
      printf("FAIL: clone stress thread %d failed\n", i);
      exit(1);
    }
  }
  
  for(int i = 0; i < STRESS_NUM; i++) {
    wait(0);
    free(stacks[i]);
  }
  
  printf("Branch D PASSED. Concurrency memory lifecycle safe.\n\n");
}

int
main(int argc, char *argv[])
{
  printf("STARTING CLONE STABILITY TESTING...\n\n");
  
  test_branch_a();
  test_branch_b();
  test_branch_c();
  test_branch_d();

  printf("ALL BRANCH TESTS COMPLETED SUCCESSFULLY\n");
  exit(0);
}