#include "types.h"
#include "stat.h"
#include "user.h"

int fill(char *ch)
{
    char *st=ch;
    for(int i=0;i<4000;i++)
    {
        *(st)= 'a';
        st++;
    }
    return 0;
}
int check(char *ch)
{
    char *st=ch;
    for(int i=0;i<4000;i++)
    {
        if(*st != 'a') return 0;
    }
    return 1;
}
int main(int argc, char *argv[])
{
    
    int N = 20;

     int pids[N];
     int rets[N];
    

    for (int i = 0; i < N; i++)
    {
        int ret = fork(); //create new child process
        if (ret == 0)
        {
            //loop with 10 iterations
            for(int j=0;j<10;j++)
            {
                //malloc and assign values in each iteration
                char *ch= (char *)malloc(sizeof(char)*4000);
                fill(ch);
                if(check(ch)==0)
                {
                    printf(1, "memory  error \n");
                }

            }
            exit();
        }
        else if (ret > 0)
        {
            pids[i] = ret;
        }
        else
        {
            printf(1, "fork error \n");
            exit();
        }
    }

    for (int i = 0; i < N; i++)
    {
        rets[i] = wait();
    }

    printf(1, "\nAll children completed\n");
    for (int i = 0; i < N; i++)
        printf(1, "Child %d.    pid %d\n", i, pids[i]);
printf(1, "\nAll children completed\n");
    for (int i = 0; i < N; i++)
        printf(1, "Child %d.    rets %d\n", i, rets[i]);

    exit();
}
