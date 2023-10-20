#include <stdio.h>
#include <unistd.h>
#include <linux/io_uring.h>
#include <liburing.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>

#define EVENT_TYPE_ACCEPT 0
#define EVENT_TYPE_RECV 1
#define EVENT_TYPE_SEND 2

struct io_uring ring;
long packetsReceived;
int duration = 5;
int batchsize = 1;

struct request{
    int type;
    struct msghdr* message;
};

void freemsg(struct msghdr * msg){
    free(msg->msg_iov->iov_base);
    free(msg->msg_iov);
    free(msg);
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
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);;
    struct request* req = malloc(sizeof(struct request));

    struct msghdr* msg = malloc(sizeof(struct msghdr));
    struct iovec* iov = malloc(sizeof(struct iovec));

    memset(msg, 0, sizeof(struct msghdr));
    memset(iov,0,sizeof(struct iovec));
    iov->iov_base = malloc(readlength);
    iov->iov_len = readlength;
    msg->msg_name = NULL;
    msg->msg_namelen = 0;
    msg->msg_iov = iov;
    msg->msg_iovlen = 1;

    req->type = EVENT_TYPE_RECV;
    req->message = msg;
    io_uring_prep_recvmsg(sqe,socket, msg,0);
    io_uring_sqe_set_data(sqe, req);
    return 1;
}

void startServer(int socketfd){
    struct io_uring_cqe* cqe = malloc(sizeof(struct io_uring_cqe) * batchsize);
    int start = 0;
    int requestsPending = 1;
    unsigned int received;

    for(int i=0;i<batchsize;i++)
        add_recv_request(socketfd,1024);
    requestsPending = batchsize;
    io_uring_submit(&ring);

    printf("Entering server loop\n");
    while (1) {
        start:
        received = io_uring_peek_batch_cqe(&ring, &cqe, batchsize);
        if (!received) {
            goto start;
        }

        if (!start) {
            start = 1;
            alarm(duration);
        }
        packetsReceived = packetsReceived + received;
            for (int i = 0; i < received; i++)
                add_recv_request(socketfd, 1024);
        io_uring_submit(&ring);

        io_uring_cqe_seen(&ring, cqe);
    }
}

void sig_handler(int signum){
    printf("\nReceived: %ld packets\n",packetsReceived);
    long speed = packetsReceived/duration;
    printf("Speed: %ld packets/second\n", speed);

    FILE* file;
    file = fopen("waitingMoreResults.txt","a");
    fprintf(file, "BATCHSIZE %d        Speed: %ld packets/second\n", batchsize,speed);
    printf("Now closing\n\n");
    fclose(file);
    io_uring_queue_exit(&ring);
    exit(0);
}

int main(int argc, char *argv[]){
    int socketfd;
    int port = 8080;

    signal(SIGALRM,sig_handler);

    packetsReceived = 0;
    if(argc >= 2)
        port= atoi(argv[1]);

    if(argc >=3)
	duration = atoi(argv[2]);

    if(argc ==4)
        batchsize = atoi(argv[3]);

    printf("Hello! Im the server!!\n");
    io_uring_queue_init(32768,&ring,0);
    socketfd = openListeningSocket(port);
    startServer(socketfd);


}
//
// Created by ebpf on 01/10/23.
//
