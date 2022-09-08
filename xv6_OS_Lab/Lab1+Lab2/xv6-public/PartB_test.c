#include "types.h"
#include "stat.h"
#include "user.h"

void delay(int count)
{
    int i;
    int j, k;
    int *data;

    data = (int *)malloc(sizeof(int) * 1024 * 10);
    if (data <= 0)
        printf(1, "Error on memory allocation \n");

    for (i = 0; i < count; i++)
    {
        for (k = 0; k < 5700; k++)
            for (j = 0; j < 1024 * 10; j++)
                data[j]++;
    }
}
int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        printf(1, "Error: Please give the no. of processes you want as argument\n");
        exit();
    }
    int N = atoi(argv[1]);
    int pids[N];
    for(int i=0; i<N; i++)pids[i] = -1;
    int rets[N];
    setBurstTime(2);
    printf(1, "Burst Time of parent process = %d\n", getBurstTime());
    int bt[N];
    for (int i = 0; i < N; i++)
    {
        bt[i]=(i*10)%19 +3;
    }
    /*bt[0]=11;
    bt[1]=13;
    bt[2]=12;
    bt[3]=15;
    bt[4]=14;*/
    for (int i = 0; i < N; i++)
    {
        int btime = bt[i];
        int ret = fork(); //create new child process
        if (ret == 0)
        {
            setBurstTime(btime); // Set process burst_times of children
            delay(btime);
            exit();
        }
        else if (ret > 0)
        {
            getBurstTime();
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
        rets[i] = wait(); //To check exit order of the processes 
    }
    
    printf(1, "\nAll children completed\n");
    for (int i = 0; i < N; i++)
        printf(1, "Child %d.    pid %d  bt %d\n", i, pids[i],bt[i]);

    printf(1, "\nExit order \n");
    for (int i = 0; i < N; i++)
        printf(1, "pid %d\n", rets[i]);

    exit();
}