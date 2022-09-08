#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

#include "processInfo.h"

#include "spinlock.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;



int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

//new function for implementation of the new call woflie 
int
sys_wolfie(void)
{
  char *wolf="\t\t,ood8888booo,\n\
                              ,od8           8bo,\n\
                           ,od                   bo,\n\
                         ,d8                       8b,\n\
                        ,o                           o,    ,a8b\n\
                       ,8                             8,,od8  8\n\
                       8'                             d8'     8b\n\
                       8                           d8'ba     aP'\n\
                       Y,                       o8'         aP'\n\
                        Y8,                      YaaaP'    ba\n\
                         Y8o                   Y8'         88\n\
                          `Y8               ,8\"           `P\n\
                            Y8o        ,d8P'              ba\n\
                       ooood8888888P\"\"\"'                  P'\n\
                    ,od                                  8\n\
                 ,dP     o88o                           o'\n\
                ,dP          8                          8\n\
               ,d'   oo       8                       ,8\n\
               $    d$\"8      8           Y    Y  o   8\n\
              d    d  d8    od  \"\"boooooooob   d\"\" 8   8\n\
              $    8  d   ood' ,   8        b  8   '8  b\n\
              $   $  8  8     d  d8        `b  d    '8  b\n\
               $  $ 8   b    Y  d8          8 ,P     '8  b\n\
               `$$  Yb  b     8b 8b         8 8,      '8  o,\n\
                    `Y  b      8o  $$o      d  b        b   $o\n\
                     8   '$     8$,,$\"      $   $o      '$o$$\n\
                      $o$$P\"                 $$o$\n\n";
 
 
  char *inp;
  int size;
  if(argint(1,&size)<0) return -1;
  if(argptr(0,&inp,size)<0) return -1;
  if(size < strlen(wolf) ) return -1;
  for(int i=0;i<strlen(wolf);i++) inp[i]=wolf[i];
  return strlen(wolf);
}


//spare wolf
//	                 .\n                    / V \\\n                  / `  /\n                 <<   |\n                 /    |\n               /      |\n             /        |\n           /    \\  \\ /\n          (      ) | |\n  ________|   _/_  | |\n<__________\\______)\\__)\n


int sys_getNumProc(void)
{
  // return getNumProc();
  int cnt=0;
  struct proc *p;
  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != UNUSED)
        cnt++;
    }
  release(&ptable.lock);
  return cnt;
  
}

int sys_getMaxPid(void)
{
  int ma=-1;
  struct proc *p;
  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state != UNUSED && p->pid > ma)
      ma=p->pid;
  }
  release(&ptable.lock);
  return ma;
}

int sys_getProcInfo(void)
{
   struct processInfo *info;
  int id;
  if(argint(0, &id) < 0 || argptr(1, (void*)&info, sizeof(struct processInfo)) < 0)
    return -1;
  return getProcInfo(id, info);
}

int sys_setBurstTime(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  return setBurstTime(n);
}

int sys_getBurstTime(void)
{
  return getBurstTime();
}