#include "kernel/types.h"
#include "user/user.h"

struct test_case {
  char *name;                 // 测试名称
  char *path;                 // 可执行程序路径
  char *argv[4];              // 参数列表
  int expected_exit_status;   // 预期的退出状态码 (0表示正常运行，-1表示预期崩溃)
};

// 测试用例
struct test_case tests[] = {
  // phase2
  { "Exception: Illegal Instruction", "crash_test", {"crash_test", "1", 0}, -1 },
  { "Exception: Invalid Read", "crash_test", {"crash_test", "2", 0}, -1 },
  { "Exception: Invalid Write", "crash_test", {"crash_test", "3", 0}, -1 },
  { "Exception: Write to Code", "crash_test", {"crash_test", "4", 0}, -1 },
  { "System call: ps", "ps", {"ps", 0}, 0 },
  { "Memory: kmalloc/kmfree", "kmalloctest", {"kmalloctest", 0}, 0 },
  // phase3
  { "Sched: Switch to FCFS", "sched", {"sched", "1", 0}, 0 },
  { "Sched: FCFS scheduling", "schedtest", {"schedtest", 0}, 0 },
  { "Sched: Switch to RR", "sched", {"sched", "0", 0}, 0 },
  { "Sched: RR scheduling", "schedtest", {"schedtest", 0}, 0 },
  { "Process: waitpid mechanism", "waitpidtest", {"waitpidtest", 0}, 0 },
  // { "Official: usertests", "usertests", {"usertests", 0}, 0 }
};

void run_test(struct test_case *tc) {
  printf("[RUN] %s...\n", tc->name);

  int pid = fork();
  if (pid < 0) {
    printf("alltests: fork failed\n");
    exit(1);
  }

  if (pid == 0) {
    // 子进程中执行对应的测试程序
    exec(tc->path, tc->argv);
    printf("alltests: exec %s failed\n", tc->path);
    exit(1);
  } else {
    // 父进程等待子进程结束，并获取其退出状态码
    int status;
    wait(&status);

    // 验证状态码
    // 注意：在xv6中，exit(x) 传入的值会被保存在 status 中
    if (tc->expected_exit_status == -1) {
      if (status == -1) {
        printf("\033[32m[PASS]\033[0m %s (Expected crash, exit status: %d)\n\n", tc->name, status);
      } else {
        printf("\033[31m[FAIL]\033[0m %s (Expected crash but returned: %d)\n\n", tc->name, status);
        exit(1); // 一个测试失败就中断集成测试
      }
    } else {
      if (status == tc->expected_exit_status) {
        printf("\033[32m[PASS]\033[0m %s (Success, exit status: %d)\n\n", tc->name, status);
      } else {
        printf("\033[31m[FAIL]\033[0m %s (Expected status %d, but got: %d)\n\n", tc->name, tc->expected_exit_status, status);
        exit(1);
      }
    }
  }
}

int main(int argc, char *argv[]) {
  printf("==========================================\n");
  printf("     INTEGRATION TEST RUNNER (ALLTESTS)   \n");
  printf("==========================================\n\n");

  int num_tests = sizeof(tests) / sizeof(struct test_case);
  for (int i = 0; i < num_tests; i++) {
    run_test(&tests[i]);
  }

  printf("==========================================\n");
  printf("\033[32m  ALL TESTS PASSED SUCCESSFULLY! (PASS: %d/%d)\033[0m\n", num_tests, num_tests);
  printf("==========================================\n");

  exit(0);
}