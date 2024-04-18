#include <kernel/types.h>
#include <kernel/stat.h>
#include <user/user.h>

#define WR 1
#define RD 0

void primes(int input_pipe){
    
    int prime_number;
    if(read(input_pipe, &prime_number, sizeof(int)) != sizeof(int)){
        exit(0);
    }
    else{
        printf("prime %d \n", prime_number);
    }

    int right_pipe[2];
    if(pipe(right_pipe) < 0){
        fprintf(2, "pipe error\n");
        exit(1);
    }
    
    int pid = fork();
    if(pid < 0){
        fprintf(2, "fork error");
        exit(1);
    }
    else if(pid > 0){
        int temp;
        close(right_pipe[RD]);
        while(read(input_pipe, &temp, sizeof(int)) == sizeof(int)){
            if(temp % prime_number != 0){
                if(write(right_pipe[WR], &temp, sizeof(int)) != sizeof(int)){
                    fprintf(2, "write %d error\n", temp);
                }
            }
        }
        close(input_pipe);
        close(right_pipe[WR]);
        wait(0);
        exit(0);
    }
    else{
        close(right_pipe[WR]);
        primes(right_pipe[RD]);
        exit(0);
    }

}

int main(int argc, char* argv[]){
    int p[2];
    if(pipe(p) < 0){
        fprintf(2, "pipe erro");
        exit(1);
    }

    int pid = fork();

    if(pid < 0){
        fprintf(2, "fork error!");
        exit(1);
    }
    else if(pid > 0){
        close(p[RD]);
        for(int i = 2; i < 36; i++){
            if(write(p[WR], &i, sizeof(int)) != sizeof(int)){
                fprintf(2, "write %d erro!\n", i);
                exit(1);
            }
        }
        close(p[WR]);
        wait(0);
        exit(0);
    }
    else{
        close(p[WR]);
        primes(p[RD]);
        exit(0);
    }

}