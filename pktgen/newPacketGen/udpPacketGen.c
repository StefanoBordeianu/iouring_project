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
#include <math.h>

struct args{
    char* ip;
    int port;
    int rate;
    int duration;
    int pktSize;
    int threads;
};

struct worker{
    int active;
    long long int pktSent;
    long long int bytesSent;
};

struct args args;

void usage(){
    /*TODO Make it nicer*/
    printf("INFO:\n");

}


int parseArgs(int argc, char* argv[]){

    int opt;

    printf("parsing\n");
    while((opt =getopt(argc,argv,"i:p:r:d:s:t:")) != -1){
        switch (opt) {
            case 'i':
                args.ip = optarg;
                break;
            case 'p':
                args.port = atoi(optarg);
                break;
            case 'r':
                args.rate = 1000000 * atoi(optarg);
                break;
            case 'd':
                args.duration = atoi(optarg);
                break;
            case 's':
                args.pktSize = atoi(optarg);
                break;
            case 't':
                args.threads = atoi(optarg);
                break;
            default:
                usage();
                return 1;
        }
    }
    printf("finished parsing\n");
    printf("Sending for %d sec.    TO %s:%d\n", args.duration,
           args.ip, args.port);
    return 0;
}

int create_socket(){
    int socket_fd;
    socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd < 1) {
        printf("Failed to open a socket!\n");
        return 1;
    }
    if (fcntl(socket_fd, F_SETFL, O_NONBLOCK)) {
        printf("Failed to set O_NONBLOCK flag!\n");
        return 1;
    }
    return socket_fd;
}

void* startThread(void* _arg){

    struct worker *wrk = _arg;
    int socketfd = create_socket();
    struct sockaddr_in addr;
    socklen_t len;
    char* buffer;
    int ret;
    clock_t start_t, end_t;
    int true_size = args.pktSize+56;
    int per_thread_rate = args.rate / args.threads * 1000;
    double total_t;

    addr.sin_family = AF_INET;
    inet_pton(AF_INET, args.ip, &addr.sin_addr);
    addr.sin_port = htons(args.port);
    len = sizeof(addr);

    wrk->bytesSent = 0;
    wrk->pktSent = 0;

    start_t = clock();
    int sent = 0;
    printf("startSending\n");
    while(wrk->active){
        if(sent >= per_thread_rate){
            end_t = clock();
            total_t = (1000000 * (double)(end_t - start_t)) / CLOCKS_PER_SEC;
            if(total_t < 1000 ) {
                total_t = round(total_t);
                usleep(1000 - total_t);
                start_t = clock();
            }
        }
        buffer = malloc(args.pktSize);
        if(!buffer){
            printf("[!]   Error allocating the send buffer\n");
            goto err;
        }
        memset(buffer,'a',args.pktSize);
        buffer[args.pktSize-1] = '\0';

        ret = sendto(socketfd,buffer,args.pktSize,0,
                     (struct sockaddr *)&addr, len);

        free(buffer);

        if (ret < 0) {
            if (errno != EWOULDBLOCK) {
                printf("Failed to send data\n %s", strerror(errno));
                goto err;
            }
            continue;
        }
        sent++;
        wrk->pktSent ++;
        wrk->bytesSent += ret;

    }

    close(socketfd);
    return NULL;

    err:
    close(socketfd);
    return (void *)-1;

}


int main(int argc, char *argv[]){

    if(parseArgs(argc, argv)){
        printf("error parsing\n");
        exit(1);
    }
    printf("parsed\n");

    pthread_t threads [args.threads];
    struct worker* workers ;

    workers = calloc(args.threads, sizeof(struct worker));
    printf("start creating\n");
    for(int i=0;i<args.threads;i++){
        workers[i].active = 1;
        printf("create\n");
        pthread_create(&threads[i], NULL, startThread, &workers[i]);
        printf("created %d",i);
    }
    printf("created threads\n");
    sleep(args.duration);
    for(int i=0;i<args.threads;i++){
        workers[i].active = 0;
    }

    for(int i=0;i<args.threads;i++){
        pthread_join(threads[i],NULL);
    }

    long int total_pkts = 0;
    long int total_bytes = 0;
    printf("          RECV        SENT\n");
    for (int i = 0; i < args.threads; i++) {
        total_pkts += workers[i].pktSent;
        total_bytes += workers[i].bytesSent;
        printf("thread %d: %lld        %lld\n", i, workers[i].pktSent, workers[i].bytesSent);
    }
    printf("total packets: %ld\n", total_pkts);
    printf("total bytes: %ld\n", total_bytes);
    printf("duration: %d\n", args.duration);
    printf("packets Throughput: %.2f\n", (double)total_pkts / (double)args.duration);
    printf("bytes Throughput: %.2f\n", (double)total_bytes / (double)args.duration);



}