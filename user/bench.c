#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void
run_cmd(char *bin, char **argv)
{
  int pid = fork();
  if(pid < 0) {
    printf("bench: fork failed\n");
    exit(-1);
  }
  if(pid == 0) {
    exec(bin, argv);
    printf("bench: exec %s failed\n", bin);
    exit(-1);
  } else {
    wait(0); 
  }
}

int
main(int argc, char *argv[])
{
  char *sched_rr[] = {"sched", "0", 0};
  char *sched_fcfs[] = {"sched", "1", 0};
  char *llama_exp1[] = {"llama", "exp1", 0};
  char *llama_exp2[] = {"llama", "exp2", 0};
  char *llama_exp3[] = {"llama", "exp3", 0};

  printf("\n==================================================\n");
  printf("       xv6-riscv Benchmark Runner       \n");
  printf("==================================================\n\n");

  // 1. RR 模式下实验一重复 3 次
  printf("[BENCH] Part 1: Configuring RR Mode...\n");
  run_cmd("sched", sched_rr);
  for(int i = 0; i < 3; i++) {
    printf("\n[BENCH] >>> RR Exp1 Trial [%d/3] <<<\n", i + 1);
    run_cmd("llama", llama_exp1);
  }

  // 2. FCFS 模式下实验一重复 3 次
  printf("\n[BENCH] Part 2: Configuring FCFS Mode...\n");
  run_cmd("sched", sched_fcfs);
  for(int i = 0; i < 3; i++) {
    printf("\n[BENCH] >>> FCFS Exp1 Trial [%d/3] <<<\n", i + 1);
    run_cmd("llama", llama_exp1);
  }

  // 3. RR 模式下实验二重复 3 次
  printf("\n[BENCH] Part 3: Configuring RR Mode...\n");
  run_cmd("sched", sched_rr);
  for(int i = 0; i < 3; i++) {
    printf("\n[BENCH] >>> RR Exp2 Trial [%d/3] <<<\n", i + 1);
    run_cmd("llama", llama_exp2);
  }

  // 4. 实验三运行 1 次
  printf("\n[BENCH] Part 4: Running Exp3 (Cold-start test)...\n");
  run_cmd("llama", llama_exp3);

  printf("\n==================================================\n");
  printf("       ALL BENCHMARKS COMPLETED!        \n");
  printf("==================================================\n\n");

  exit(0);
}