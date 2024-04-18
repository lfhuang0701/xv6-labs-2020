#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
    if(argc != 2){
        fprintf(1, "must 1 argument for sleep!");
        exit(1);
    }
    int sleepNum = atoi(argv[1]);
    printf("(nothing happen for a little while)\n");
    sleep(sleepNum);
    printf("sleep %d success\n", sleepNum);
    exit(0);

}