#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  if(kmalloctest() < 0){
    printf("kmalloctest: failed\n");
    exit(1);
  }
  printf("kmalloctest: OK\n");
  exit(0);
}