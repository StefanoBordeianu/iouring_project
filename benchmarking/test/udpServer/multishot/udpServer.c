#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <errno.h>

#include "liburing.h"

#define BUF_GRPID 37
#define BUFF_SIZE 1500


struct io_uring ring;
long packetsReceived;
long bytes_rec;
char** buffers;

struct request{
    int type;
    char* message;
};

struct args{
    int port;
    int numb_of_buffers;
    int duration;
    int size;
    int debug;
    int batching;
};

struct args args;

int parseArgs(int argc, char* argv[]){
    int opt;
    args.port = 2020;
    args.batching = 256;
    args.duration = 10;
    args.numb_of_buffers = 10000;
    args.debug = 0;

    while((opt =getopt(argc,argv,"hs:p:d:n:b:v")) != -1) {
        switch (opt) {
            case 'p':
                args.port = atoi(optarg);
                break;
            case 'n':
                args.numb_of_buffers =  atoi(optarg);
                break;
            case 'b':
                args.batching = atoi(optarg);
                break;
            case 'd':
                args.duration = atoi(optarg);
                break;
            case 'v':
                args.debug = 1;
                break;
            case 's':
                args.size = atoi(optarg);
                break;
        }
    }
    return 0;
}

struct io_uring_buf_ring* initialize_buffers(){

    struct io_uring_buf_reg reg = {};
    struct io_uring_buf_ring *br;
    int i;
    printf("SIZE: %ld\nSCpagesize:  %ld\n",
           (args.numb_of_buffers * sizeof(struct io_uring_buf)),sysconf(_SC_PAGESIZE));
    if (posix_memalign((void **) &br, sysconf(_SC_PAGESIZE),
                       args.numb_of_buffers * sizeof(struct io_uring_buf))){
        printf("PRIMO %s\n",strerror(errno));
        return NULL;
    }

    reg.ring_addr = (unsigned long) br;
    reg.ring_entries = args.numb_of_buffers;
    reg.bgid = BUF_GRPID;
    // STA MERDA DA INVALID ARGUMENT PER QUALCHE MOTIVO PORCODDIO LADRO
    //printf("br  %x\n",(unsigned long)br);
    if ((i = io_uring_register_buf_ring(&ring, &reg, 0))){
        printf("register error:     %s\n",strerror(i*-1));
        return NULL;
    }

    buffers = malloc(sizeof(char*) * args.numb_of_buffers);
    for(i=0;i<args.numb_of_buffers;i++)
        buffers[i] = malloc(BUFF_SIZE);

    io_uring_buf_ring_init(br);
    for (i = 0; i < args.numb_of_buffers; i++) {
        io_uring_buf_ring_add(br, buffers[i], BUFF_SIZE, i,
                              io_uring_buf_ring_mask(args.numb_of_buffers),
                              i);
    }

    io_uring_buf_ring_advance(br, args.numb_of_buffers);
    return br;
}

int openListeningSocket(int port){
    int socketfd;
    struct sockaddr_in add;

    socketfd = socket(AF_INET, SOCK_DGRAM, 0);
    if(socketfd < 0){
        printf("SERVER: Error while creating the socket\n");
        exit(-1);
    }

    add.sin_port = htons(port);
    add.sin_family = AF_INET;
    add.sin_addr.s_addr = INADDR_ANY;
    if(bind(socketfd,(struct sockaddr *)&add, sizeof(add)) < 0){
        perror("bind()");
        exit(-1);
    }
    return socketfd;
}

int add_recv_request(int socket){
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);
    io_uring_prep_recv_multishot(sqe, socket, NULL,0,0);
    sqe->buf_group = BUF_GRPID;
    io_uring_sqe_set_flags(sqe, IOSQE_BUFFER_SELECT);
    return 1;
}

void startServer(int socketfd){
    struct io_uring_cqe* cqe[args.numb_of_buffers];
    unsigned int reaped, buffer_id;
    int start = 0;
    struct io_uring_buf_ring* br;
    br = initialize_buffers();

    add_recv_request(socketfd);
    io_uring_submit(&ring);
    printf("entering server\n");
    while (1) {
        start:
        reaped = io_uring_peek_batch_cqe(&ring,cqe,args.numb_of_buffers);
        if(!reaped){
            goto start;
        }
        if (!start) {
            start = 1;
            alarm(args.duration);
        }
        packetsReceived += reaped;
        for (int i = 0; i < reaped; i++) {
            if (!(cqe[i]->flags & IORING_CQE_F_MORE)) {
                if(args.debug)
                    printf("readding multishot\n");
                add_recv_request(socketfd);
                io_uring_submit(&ring);
            }
            if(cqe[i]->res == -ENOBUFS){
                if(args.debug)
                    printf("readding multishot\n");
                continue;
            }
            bytes_rec += cqe[i]->res;
            buffer_id = cqe[i]->flags >> IORING_CQE_BUFFER_SHIFT;
            io_uring_buf_ring_add(br, buffers[buffer_id], BUFF_SIZE, buffer_id,
                                  io_uring_buf_ring_mask(args.numb_of_buffers), i);
        }
        io_uring_buf_ring_cq_advance(&ring,br,(int)reaped);
    }
}

void sig_handler(int signum){
    printf("\nReceived: %ld packets of size %d\n",packetsReceived, args.size);
    long speed = packetsReceived/args.duration;
    printf("Speed: %ld packets/second\n", speed);
    printf("Rate: %ld Mb/s\n", (bytes_rec*8)/(args.duration * 1000000));
    printf("Now closing\n\n");
    io_uring_queue_exit(&ring);
    exit(0);
}


int main(int argc, char *argv[]){
    int socketfd;

    parseArgs(argc, argv);
    signal(SIGALRM,sig_handler);

    io_uring_queue_init(32768,&ring,0);
    socketfd = openListeningSocket(args.port);


    startServer(socketfd);

}

