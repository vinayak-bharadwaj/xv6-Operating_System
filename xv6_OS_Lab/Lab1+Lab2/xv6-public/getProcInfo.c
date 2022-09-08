#include "types.h"
#include "stat.h"
#include "processInfo.h"
#include "user.h"




//user program to execute the system call wolfie
int main(int argc, char *argv[])
{
  struct processInfo info;
  int pid;
  printf(1, "PID\tPPID\tSIZE\tNumber of Context Switch\n");
  // To get processinfo of each process with no. of context switches
  for(int i=0; i<=getMaxPid(); i++)
  {
    pid = i;
    if(getProcInfo(pid, &info) == 0)
    printf(1, "%d\t%d\t%d\t%d\n", pid, info.ppid, info.psize, info.numberContextSwitches);
  }
  exit();
  return 0;
}