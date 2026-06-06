#include "kernel/types.h"
#include "user/user.h"

// 对应 kernel/proc.h 中的状态枚举
static char *states[] = {
  [0] "UNUSED  ",
  [1] "USED    ",
  [2] "SLEEPING",
  [3] "RUNNABLE",
  [4] "RUNNING ",
  [5] "ZOMBIE  "
};

int
main(int argc, char *argv[])
{
  struct uproc procs[64];
  int count = getprocs(64, procs);
  if(count < 0){
    printf("ps: failed to get process info\n");
    exit(1);
  }

  // 打印表头
  printf("PID    STATE       SIZE       NAME\n");
  for(int i = 0; i < count; i++){
    char *state = "UNKNOWN ";
    if(procs[i].state >= 0 && procs[i].state < 6) {
      state = states[procs[i].state];
    }
    printf("%d      %s    %d       %s\n", 
           procs[i].pid, state, procs[i].sz, procs[i].name);
  }
  exit(0);
}