#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <liburing.h>

#define BUF_GRPID 37
#define BUFF_SIZE 1500
#define NUMBER_OF_BUFFERS args.numb_of_buffers

struct io_uring ring;
long packetsReceived;
long bytes_rec;
char** buffers;

struct request{
    int type;
    struct msghdr* message;
};

struct args{
    int port;
    int batching;
    int duration;
    int size;
    int debug;
    int numb_of_buffers
};

struct args args;

void freemsg(struct msghdr * msg){
    free(msg->msg_iov->iov_base);
    free(msg->msg_iov);
    free(msg);
}

void parseArgs(int argc, char* argv[]){
    int opt;
    args.port = 2020;
    args.batching = 1;
    args.duration = 10;

    while((opt =getopt(argc,argv,"hs:p:d:b:")) != -1) {
        switch (opt) {
            case 'p':
                args.port = atoi(optarg);
                break;
            case 'b':
                args.batching =  atoi(optarg);
                break;
            case 'd':
                args.duration = atoi(optarg);
                break;
            case 's':
                args.size = atoi(optarg);
                break;
            case 'n':
                args.numb_of_buffers = atoi(optarg);
                break;
        }
    }
}

struct io_uring_buf_ring* initialize_buffers(){

    struct io_uring_buf_reg reg = {};
    struct io_uring_buf_ring *br;
    int i;
    printf("SIZE: %ld\nSCpagesize:  %ld\n",
           (NUMBER_OF_BUFFERS * sizeof(struct io_uring_buf)),sysconf(_SC_PAGESIZE));
    if (posix_memalign((void **) &br, sysconf(_SC_PAGESIZE),
                       NUMBER_OF_BUFFERS * sizeof(struct io_uring_buf))){
        printf("PRIMO %s\n",strerror(errno));
        return NULL;
    }

    reg.ring_addr = (unsigned long) br;
    reg.ring_entries = NUMBER_OF_BUFFERS;
    reg.bgid = BUF_GRPID;
    if ((i = io_uring_register_buf_ring(&ring, &reg, 0))){
        printf("register error:     %s\n",strerror(i*-1));
        return NULL;
    }

    buffers = malloc(sizeof(char*) * NUMBER_OF_BUFFERS);
    for(i=0;i<NUMBER_OF_BUFFERS;i++)
        buffers[i] = malloc(BUFF_SIZE);

    io_uring_buf_ring_init(br);
    for (i = 0; i < NUMBER_OF_BUFFERS; i++) {
        io_uring_buf_ring_add(br, buffers[i], BUFF_SIZE, i,
                              io_uring_buf_ring_mask(NUMBER_OF_BUFFERS),
                              i);
    }

    io_uring_buf_ring_advance(br, NUMBER_OF_BUFFERS);
    return br;
}

int openListeningSocket(int port){
    int socketfd;
    int opt = 1;
    struct sockaddr_in add;

    socketfd = socket(AF_INET, SOCK_DGRAM, 0);
    if(socketfd < 0){
        printf("SERVER: Error while creating the socket\n");
        exit(-1);
    }
    if(setsockopt(socketfd,SOL_SOCKET,SO_REUSEADDR|SO_REUSEPORT,
                  &opt,sizeof (opt))){
        printf("SERVER: Socket options error\n");
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

int add_recv_request(int socket, long readlength){
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);

//    struct msghdr* msg = malloc(sizeof(struct msghdr));
//    struct iovec* iov = malloc(sizeof(struct iovec));
//
//    memset(msg, 0, sizeof(struct msghdr));
//    memset(iov,0,sizeof(struct iovec));
//    iov->iov_base = malloc(readlength);
//    iov->iov_len = readlength;
//    msg->msg_name = NULL;
//    msg->msg_namelen = 0;
//    msg->msg_iov = iov;
//    msg->msg_iovlen = 1;

//    req->message = msg;
    io_uring_prep_recvmsg(sqe,socket, NULL,0);
    sqe->buf_group = BUF_GRPID;
    io_uring_sqe_set_flags(sqe, IOSQE_BUFFER_SELECT);
    return 1;
}

void resupply_buffer(struct io_uring_cqe* cqe, int offset, struct io_uring_buf_ring* br){
    unsigned int buffer_id;

    buffer_id = cqe->flags >> IORING_CQE_BUFFER_SHIFT;
    io_uring_buf_ring_add(br, buffers[buffer_id], BUFF_SIZE, buffer_id,
                          io_uring_buf_ring_mask(NUMBER_OF_BUFFERS), offset);
}

void startServer(int socketfd){
    struct io_uring_cqe* cqe;
    int start = 0;
    struct io_uring_buf_ring* br;
    br = initialize_buffers();

    add_recv_request(socketfd,1500);
    io_uring_submit(&ring);

    printf("Entering server loop\n");
    while(1){
        if(io_uring_wait_cqe(&ring, &cqe)){
            printf("ERROR WAITING\n");
            exit(-1);
        }
        if(!start) {
            start = 1;
            alarm(args.duration);
            printf("alarm set\n");
        }

        packetsReceived++;
        bytes_rec += cqe->res;
        add_recv_request(socketfd,1500);
        io_uring_submit(&ring);
        resupply_buffer(cqe,0,br);
        io_uring_buf_ring_cq_advance(&ring,br,1);
    }
}

void startBatchingServer(int socketfd){
    struct io_uring_cqe* cqe [args.batching];
    int start = 0;
    unsigned int packets_rec;
    int rec;
    struct io_uring_buf_ring* br;
    br = initialize_buffers();

    for(int i=0;i<args.batching;i++)
        add_recv_request(socketfd,1500);
    io_uring_submit(&ring);

    printf("Entering server loop\n");
    while (1) {
        start:
        packets_rec = io_uring_peek_batch_cqe(&ring, cqe, args.batching);
        if (!packets_rec) {
            goto start;
        }
        if (!start) {
            start = 1;
            alarm(args.duration);
        }
        packetsReceived = packetsReceived + packets_rec;
        for (int i = 0; i < packets_rec; i++) {
            add_recv_request(socketfd, 1024);
            bytes_rec += cqe[i]->res;
            resupply_buffer(cqe[i],i,br);
        }
        io_uring_submit(&ring);
        io_uring_buf_ring_cq_advance(&ring,br,packets_rec);

    }
}

void sig_handler(int signum){
    printf("\nReceived: %ld packets of size %d\n",packetsReceived, args.size);
    long speed = packetsReceived/args.duration;
    printf("Speed: %ld packets/second\n", speed);
    printf("Rate: %ld Mb/s\n", (bytes_rec*8)/(args.duration * 1000000));
    printf("Now closing\n\n");
    FILE* file = fopen("standardServerResults.txt","a");
    fprintf(file, "%ld\n", speed);
    fprintf(file,"%f\n", ((double)(bytes_rec*8))/(args.duration * 1000000));
    fclose(file);
    io_uring_queue_exit(&ring);
    exit(0);
}

int main(int argc, char *argv[]){
    int socketfd;

    parseArgs(argc, argv);
    signal(SIGALRM,sig_handler);

    io_uring_queue_init(32768,&ring,0);
    socketfd = openListeningSocket(args.port);

    if(args.batching == 1){
        printf("starting standard server\n");
        startServer(socketfd);
    }
    else {
        printf("starting batching server\n");
        startBatchingServer(socketfd);
    }

}