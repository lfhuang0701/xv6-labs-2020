#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"


int main(int argc, char* argv[]){
    int parentToChild[2];
    int childToparent[2];

    char buf = 'p';

    if(pipe(parentToChild) < 0){
        fprintf(2, "pipe parentToChild erro!");
    }

    if(pipe(childToparent) < 0){
        fprintf(2, "pipe childToparent erro!");
    }

    int pid = fork();
    int exit_status = 0;
    if(pid < 0){
        fprintf(2, "fork erro");
        exit(1);
    }
    else if(pid == 0){
        close(childToparent[0]);
        close(parentToChild[1]);
        if(read(parentToChild[0], &buf, sizeof(char)) != sizeof(char)){
            fprintf(2, "child read erro!\n");
            exit_status = 1;
        }
        else{
            fprintf(1, "%d: received ping\n", getpid());
        }

        if(write(childToparent[1], &buf, sizeof(char)) != sizeof(char)){
            fprintf(2, "child write erro!\n");
            exit_status = 1;
        }

        close(childToparent[1]);
        close(parentToChild[0]);
        exit(exit_status);

    }
    else{
        close(childToparent[1]);
        close(parentToChild[0]);

        if(write(parentToChild[1], &buf, sizeof(char)) != sizeof(char)){
            fprintf(2, "parent write erro!\n");
            exit_status = 1;
        }

        if(read(childToparent[0], &buf, sizeof(char)) != sizeof(char)){
            fprintf(2, "parent read erro!\n");
            exit_status = 1;
        }
        else{
            fprintf(1, "%d: received pong\n", getpid());
        }

        close(childToparent[0]);
        close(parentToChild[1]);
        exit(exit_status);

    }

}