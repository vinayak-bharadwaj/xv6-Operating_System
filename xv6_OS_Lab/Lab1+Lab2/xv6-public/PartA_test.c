#include "types.h"
#include "stat.h"
#include "processInfo.h"
#include "user.h"




//user program to execute the system call wolfie
int main(int argc, char *argv[])
{
  int no=getNumProc();//get no. of processes
  int mp=getMaxPid();//get maxpid in processes
  printf(1,"number of proc %d\n", no);
  printf(1,"max pid %d\n", mp);
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
  int b=setBurstTime(10);
  printf(1,"setBT %d\n", b);
  printf(1,"BT %d\n", getBurstTime());
  exit();
  return 0;
}