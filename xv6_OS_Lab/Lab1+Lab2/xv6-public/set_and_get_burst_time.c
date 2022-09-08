#include "types.h"
#include "stat.h"
#include "processInfo.h"
#include "user.h"




//user program to execute the system call wolfie
int main(int argc, char *argv[])
{
  if (argc < 2)
  {
    printf(1, "Error: Please give the burst time you want to set as argument\n");
    exit();
  }
  int k=atoi(argv[1]);
  setBurstTime(k);
  printf(1,"BT %d\n", getBurstTime());
  exit();
  return 0;
}