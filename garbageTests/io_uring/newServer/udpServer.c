#include <stdio.h>
#include <linux/io_uring.h>
#include <liburing.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/tcp.h>

#define EVENT_TYPE_ACCEPT 0
#define EVENT_TYPE_RECV 1
#define EVENT_TYPE_SEND 2

struct io_uring ring;

struct request{
    int type;
    struct msghdr* message;
};

void freemsg(struct msghdr * msg){
    free(msg->msg_name);
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
    struct sockaddr_in client_address;
    char buffer [readlength];
    struct msghdr msg;
    struct iovec iov[1];

    printf("1\n");
    memset(&msg, 0, sizeof(msg));
    memset(&iov,0,sizeof(iov));
    printf("2\n");
    iov[0].iov_base = buffer;
    iov[0].iov_len = sizeof(buffer);
    printf("3\n");
    msg.msg_name = &client_address;
    msg.msg_namelen = (socklen_t) sizeof(client_address);
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;
    printf("4\n");

    req->type = EVENT_TYPE_RECV;
    req->message = &msg;
    io_uring_prep_recvmsg(sqe,socket, &msg,0);
    io_uring_sqe_set_data(sqe, req);
    io_uring_submit(&ring);
    printf("5\n");
    return 1;
}

int add_send_request(int socket, char* toSend, struct sockaddr_in* client_addr){
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);
    struct request* req = malloc(sizeof(struct request));
    struct msghdr msg;
    struct iovec iov[1];
    struct sockaddr_in client_address = *client_addr;

    memset(&msg, 0, sizeof(msg));
    memset(&iov,0,sizeof(iov));
    iov[0].iov_base = toSend;
    iov[0].iov_len = strlen(toSend);
    msg.msg_name = &client_address;
    msg.msg_namelen = sizeof(client_address);
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;

    req->type = EVENT_TYPE_SEND;

    io_uring_prep_sendmsg(sqe, socket, &msg ,0);
    io_uring_sqe_set_data(sqe, req);
    io_uring_submit(&ring);
    return 1;
}

void startServer(int socketfd){
    char* toSend = "Server says hi!";
    char* msg_received;
    add_recv_request(socketfd,1024);

    printf("Entering server loop\n");
    while(1){
        printf("6\n");
        struct io_uring_cqe* cqe;
        io_uring_wait_cqe(&ring, &cqe);
        printf("7\n");
        struct request* req = io_uring_cqe_get_data(cqe);

        printf("8\n");
        switch (req->type) {
            case EVENT_TYPE_RECV:
                printf("received 1\n");
                //msg_received = (char*) req->message->msg_iov->iov_base;
                //printf("RECEIVED from socket %d: %s\n",socketfd, msg_received);
                add_send_request(socketfd,toSend,
                                 (struct sockaddr_in *) req->message->msg_name);
                add_recv_request(socketfd,1024);
                //free(req->message);
                free(req);
                break;
            case EVENT_TYPE_SEND:
                printf("sent a packet!\n");
                free(req);
                break;

        }
        io_uring_cqe_seen(&ring, cqe);
    }
}

int main(){
    int socketfd;
    int port;

    printf("Hello! Im the server!!\n");
    io_uring_queue_init(1024,&ring,0);
    socketfd = openListeningSocket(8080);
    startServer(socketfd);


}
//
// Created by ebpf on 01/10/23.
//
