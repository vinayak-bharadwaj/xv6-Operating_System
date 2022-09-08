#include "types.h"
#include "stat.h"
#include "processInfo.h"
#include "user.h"




//user program to execute the system call wolfie
int main(int argc, char *argv[])
{
  int mp=getMaxPid();//get maxpid in processes
  printf(1,"max pid %d\n", mp);
  exit();
  return 0;
}