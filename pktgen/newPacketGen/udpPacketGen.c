#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <getopt.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <time.h>
#include <pthread.h>
#include <sys/fcntl.h>

struct args{
    char* ip;
    int port;
    double rate;
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
int bigger_slices;


void usage(){
    /*TODO Make it nicer*/
    printf("INFO:\n-i IP\n-p PORT\n-r RATE\n-d DURATION\n-s PACKET SIZE\n-t NUMBER OF THREADS\n");

}

int parseArgs(int argc, char* argv[]){
    int opt;
    while((opt =getopt(argc,argv,"hi:p:r:d:s:t:")) != -1){
        switch (opt) {
            case 'i':
                args.ip = optarg;
                break;
            case 'p':
                args.port = atoi(optarg);
                break;
            case 'r':
                args.rate =  (strtod(optarg,NULL) / 8) ; //NO IDEA WHY BUT IT NEEDS 10*
                break;
            case 'd':
                args.duration = atoi(optarg);
                break;
            case 's':
                args.pktSize = atoi(optarg);
                if(args.pktSize >= 10000)
                    bigger_slices = 10;
                else
                    bigger_slices = 1;
                break;
            case 't':
                args.threads = atoi(optarg);
                break;
            case 'h':
                usage();
                return -1;
            default:
                usage();
                return 1;
        }
    }
    printf("Sending for %d sec.    TO %s:%d\nRate: %.0f\n", args.duration,
           args.ip, args.port,args.rate*1000000);
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

void* sendThread(void* _arg){

    struct worker *wrk = _arg;
    int socketfd = create_socket();
    struct sockaddr_in addr;
    socklen_t len;
    char* buffer;
    int ret, sleep_ret;
    long time_taken;
    struct timespec start_t, end_t, sleep_for;
    int size_with_headers = args.pktSize+46;
    //
    // int size_with_headers = args.pktSize;
    long per_thread_rate = (long) (1000000 * args.rate) / (args.threads * (100/bigger_slices)); //10ms slices
    long packets_per_slice = per_thread_rate/size_with_headers;

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
    printf("startSending  %ld  %ld PPcs\n", per_thread_rate, packets_per_slice);

    clock_gettime(CLOCK_REALTIME, &start_t);
    while(wrk->active){

        if(sent >= packets_per_slice){              //sent the ammount for slice
            sent = 0;
            clock_gettime(CLOCK_REALTIME, &end_t);
            time_taken = ((end_t.tv_sec - start_t.tv_sec) * 1000000000) + (end_t.tv_nsec - start_t.tv_nsec);
            if(time_taken < 10000000 * bigger_slices ) {
                sleep_for.tv_sec = 0;
                sleep_for.tv_nsec = (10000000 * bigger_slices) - time_taken;

                sleep_ret = nanosleep(&sleep_for,NULL);
                if(sleep_ret){
                    if (errno == EFAULT)
                        printf("1\n %s", strerror(errno));
                    if (errno == EINTR)
                        printf("2\n %s", strerror(errno));
                    if (errno == EINVAL)
                        printf("3\n %s", strerror(errno));

                }
                clock_gettime(CLOCK_REALTIME, &start_t);
            }
        }

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
        ret += 46;
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

    int i;
    if((i = parseArgs(argc, argv))!=0){
        if(i>0) {
            printf("error parsing\n");
            exit(1);
        }
        else
            return 1;
    }

    pthread_t threads [args.threads];
    struct worker* workers ;

    workers = calloc(args.threads, sizeof(struct worker));
    for(i=0;i<args.threads;i++){
        workers[i].active = 1;
        pthread_create(&threads[i], NULL, sendThread, &workers[i]);
    }
    sleep(args.duration);
    for(i=0;i<args.threads;i++){
        workers[i].active = 0;
    }

    for(i=0;i<args.threads;i++){
        pthread_join(threads[i],NULL);
    }

    long int total_pkts = 0;
    long int total_bits = 0;
    printf("          PACKETS        BYTES\n");
    for (i = 0; i < args.threads; i++) {
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