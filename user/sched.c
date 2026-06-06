#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  if(argc < 2){
    printf("Usage: sched <mode> (0: RR, 1: FCFS)\n");
    exit(1);
  }
  int mode = atoi(argv[1]);
  if(sched_switch(mode) < 0){
    printf("sched: failed to switch mode\n");
    exit(1);
  }
  printf("sched: switched to %s mode\n", mode == 0 ? "RR" : "FCFS");
  exit(0);
}