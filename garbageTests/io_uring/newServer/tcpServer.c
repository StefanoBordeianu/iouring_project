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
    int socket;
    char* buff;
};

int openListeningSocket(int port){
    int socketfd;
    int opt = 1;
    struct sockaddr_in add;

    socketfd = socket(AF_INET, SOCK_STREAM, 0);
    if(socketfd < 0){
        printf("SERVER: Error while creating the socket\n");
        exit(-1);
    }
    if(setsockopt(socketfd,SOL_SOCKET,SO_REUSEADDR|SO_REUSEPORT|TCP_NODELAY,
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
    if(listen(socketfd, 2) < 0){
        perror("listen()");
        exit(-1);
    }
    return socketfd;
}

int add_accept_request(int socketfd, struct sockaddr_in* addr, socklen_t* len){
    struct io_uring_sqe* sqe;
    sqe = io_uring_get_sqe(&ring);
    struct request* req = malloc(sizeof(struct request));
    req->type = EVENT_TYPE_ACCEPT;
    io_uring_prep_accept(sqe,socketfd,(struct sockaddr*) addr,len,0);
    io_uring_sqe_set_data(sqe, req);
    io_uring_submit(&ring);
    return 1;
}

int add_recv_request(int socket, long readlength){
    struct io_uring_sqe* sqe;
    struct request* req = malloc(sizeof(struct request));
    sqe = io_uring_get_sqe(&ring);
    req->buff = malloc(readlength);
    req->socket = socket;
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
    req->socket = socket;
    io_uring_prep_send(sqe, socket, toSend, strlen(toSend),0);
    io_uring_sqe_set_data(sqe, req);
    io_uring_submit(&ring);
    return 1;
}

void startServer(int socketfd){
    struct sockaddr_in clientaddr;
    socklen_t addlen = sizeof(clientaddr);
    char* toSend = "Server says hi!";

    add_accept_request(socketfd,&clientaddr,&addlen);
    printf("Entering server loop\n");
    while(1){
        struct io_uring_cqe* cqe;
        io_uring_wait_cqe(&ring, &cqe);
        struct request* req = io_uring_cqe_get_data(cqe);

        switch (req->type) {
            case EVENT_TYPE_ACCEPT:
                printf("received accept request\n");
                add_accept_request(socketfd, &clientaddr, &addlen);
                add_recv_request(cqe->res,1024);
                free(req);
                break;
            case EVENT_TYPE_RECV:
                printf("RECEIVED from socket %d: %s\n",req->socket,req->buff);
                add_send_request(req->socket,toSend);
                free(req->buff);
                free(req);
                break;
            case EVENT_TYPE_SEND:
                printf("sent a packet!\n");
                add_recv_request(req->socket,1024);
                free(req);
                break;

        }
        io_uring_cqe_seen(&ring, cqe);
    }
}

int main(int argc, char *argv[]){
    int socketfd;
    int port = 8080;

    if(argc == 2)
        port= atoi(argv[1]);

    printf("Hello! Im the server!!\n");
    io_uring_queue_init(32768,&ring,0);
    socketfd = openListeningSocket(port);
    startServer(socketfd);


}
