#include "types.h"
#include "stat.h"
#include "processInfo.h"
#include "user.h"




//user program to execute the system call wolfie
int main(int argc, char *argv[])
{
  int no=getNumProc();//get no. of processes
  printf(1,"number of proc %d\n", no);
  exit();
  return 0;
}