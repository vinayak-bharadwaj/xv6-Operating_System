#include "types.h"
#include "stat.h"
#include "processInfo.h"
#include "user.h"




//user program to execute the system call wolfie
int
main(int argc, char *argv[])
{
  // if(argc < 2){
  //   //this means user did NOT enter a number 
  //   printf(1,"enter a number with it\n");
  //   exit();
  // }
  // char *a;
  // int size=atoi(argv[1]);              //converting the accepted size to integer 
  // printf(1,"%d\n",size);
  // a=(char *)malloc(sizeof(char)*size); //allocating the buffer 
  // int x=wolfie(a,size);                //invoking the system call wolfie 
  // if(x>0)printf(1,a,x);                // wolfie was printed
  // else printf(1,"Error input larger number\n"); 
  int no=getNumProc();
  int mp=getMaxPid();
  struct processInfo *st;
  st= (struct processInfo *)malloc(sizeof(struct processInfo));
  st->numberContextSwitches=0;


 getProcInfo(2,st);

  printf(1,"number of proc %d\n", no);
   printf(1,"max pid %d\n", mp);
   printf(1,"val of getproc info %d\n", st->numberContextSwitches);
   //setBurstTime(10);
   struct processInfo info;
    int pid;
    printf(1, "PID\tPPID\tSIZE\tNumber of Context Switch\n");
    for(int i=0; i<=getMaxPid(); i++)
    {
        pid = i;
        if(getProcInfo(pid, &info) == 0)
    printf(1, "%d\t%d\t%d\t%d\n", pid, info.ppid, info.psize, info.numberContextSwitches);
    }
    
  // fork();
  int v=setBurstTime(10);
   printf(1,"setBT %d\n", v);
   printf(1,"BT %d\n", getBurstTime());
  //  //exit();
  //  wait();
   exit();
 
  return 0;
}
