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
clock_t current_time;


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
                args.rate =  atoi(optarg) / 8 ;
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

void* updateClock(void* _arg){

    struct worker *wrk = _arg;
    while(wrk->active){
        current_time = clock();
    }
    return NULL;
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
    long per_thread_rate = (1000000 * args.rate) / (args.threads * 1000);
    long toSend = per_thread_rate/true_size;
    double total_t;

    addr.sin_family = AF_INET;
    inet_pton(AF_INET, args.ip, &addr.sin_addr);
    addr.sin_port = htons(args.port);
    len = sizeof(addr);

    wrk->bytesSent = 0;
    wrk->pktSent = 0;

    int sent = 0;
    buffer = malloc(args.pktSize);
    memset(buffer,'a',args.pktSize);
    buffer[args.pktSize-1] = '\0';
    printf("startSending  %ld  %ld PPms\n", per_thread_rate, toSend);

    start_t = current_time;
    while(wrk->active){
        if(sent >= toSend){
            sent = 0;
            end_t = current_time;
            total_t = (1000000 * (double)(end_t - start_t)) / CLOCKS_PER_SEC;
            //printf("%.2f\n",total_t);
            start_t = current_time;
            if(total_t < 1000 ) {
                //printf("LESS");
                total_t = round(total_t);
                usleep(1000 - total_t);
            }
        }

//        buffer = malloc(args.pktSize);
//        if(!buffer){
//            printf("[!]   Error allocating the send buffer\n");
//            goto err;
//        }
//        memset(buffer,'a',args.pktSize);
//        buffer[args.pktSize-1] = '\0';

        ret = sendto(socketfd,buffer,args.pktSize,0,
                     (struct sockaddr *)&addr, len);


        if (ret < 0) {
            if (errno != EWOULDBLOCK) {
                printf("Failed to send data\n %s", strerror(errno));
                goto err;
            }
            continue;
        }
        sent++;
        wrk->pktSent ++;
        ret += 56;
        wrk->bytesSent += ret;
    }
    free(buffer);
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

    pthread_t threads [args.threads];
    struct worker* workers ;
    pthread_t clock_t;
    struct worker* clock_worker = malloc(sizeof(struct worker));
    clock_worker->active = 1;

    pthread_create(&clock_t,0,updateClock,clock_worker);

    workers = calloc(args.threads, sizeof(struct worker));
    for(int i=0;i<args.threads;i++){
        workers[i].active = 1;
        pthread_create(&threads[i], NULL, startThread, &workers[i]);
        printf("created %d\n",i);
    }
    printf("created threads\n");
    sleep(args.duration);
    for(int i=0;i<args.threads;i++){
        workers[i].active = 0;
    }
    clock_worker->active = 0;

    for(int i=0;i<args.threads;i++){
        pthread_join(threads[i],NULL);
    }
    pthread_join(clock_t,NULL);

    long int total_pkts = 0;
    long int total_bits = 0;
    printf("          PACKETS        BYTES\n");
    for (int i = 0; i < args.threads; i++) {
        total_pkts += workers[i].pktSent;
        total_bits += workers[i].bytesSent*8;
        printf("thread %d: %lld        %lld\n", i, workers[i].pktSent, workers[i].bytesSent);
    }
    printf("total packets: %ld\n", total_pkts);
    printf("total bits: %.2f Gb\n", (double)total_bits / 1000000000);
    printf("duration: %d\n", args.duration);
    printf("packets Throughput: %.2f\n", (double)total_pkts / (double)args.duration);
    printf("bits Throughput: %.2f Mb/s\n", (double)total_bits / ((double)args.duration * 1000000));



}