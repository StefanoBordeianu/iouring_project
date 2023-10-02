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
    char* buff;
};

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
    struct io_uring_sqe* sqe;
    struct request* req = malloc(sizeof(struct request));
    sqe = io_uring_get_sqe(&ring);
    req->buff = malloc(readlength);
    req->type = EVENT_TYPE_RECV;
    memset(req->buff, 0, readlength);
    io_uring_prep_recv(sqe,socket,req->buff,readlength,0);
    io_uring_sqe_set_data(sqe, req);
    io_uring_submit(&ring);
    return 1;
}

int add_send_request(int socket, char* toSend){
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);
    struct request* req = malloc(sizeof(struct request));
    req->type = EVENT_TYPE_SEND;
    io_uring_prep_send(sqe, socket, toSend, strlen(toSend),0);
    io_uring_sqe_set_data(sqe, req);
    io_uring_submit(&ring);
    return 1;
}

void startServer(int socketfd){
    struct sockaddr_in clientaddr;
    socklen_t addlen = sizeof(clientaddr);
    char* toSend = "Server says hi!";

    add_recv_request(socketfd,1024);
    printf("Entering server loop\n");
    while(1){
        struct io_uring_cqe* cqe;
        io_uring_wait_cqe(&ring, &cqe);
        struct request* req = io_uring_cqe_get_data(cqe);
        switch (req->type) {
            case EVENT_TYPE_RECV:
                printf("RECEIVED from socket %d: %s\n",socketfd,req->buff);
                add_send_request(socketfd,toSend);
                free(req->buff);
                free(req);
                break;
            case EVENT_TYPE_SEND:
                printf("sent a packet!\n");
                add_recv_request(socketfd,1024);
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
