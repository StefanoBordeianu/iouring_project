#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <getopt.h>

#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <sys/fcntl.h>

#include <poll.h>

#include <time.h>
#include <pthread.h>

int main(){

    for(int i=0;i<1000;i++){
        i += 1;
    }

    long c = clock();
    long b = clock();
    //long a = clock();

    printf("%ld",CLOCKS_PER_SEC);
    printf("%ld,       %f\n",c,((double)c/CLOCKS_PER_SEC*1000000));
    printf("%ld,       %f\n",b,((double)b/CLOCKS_PER_SEC*1000000));
    long a = clock();
    printf("%ld,       %f\n",a,((double)a/CLOCKS_PER_SEC*1000000));
}