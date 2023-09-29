#include <stdio.h>
#include <linux/io_uring.h>
#include <liburing.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>

#define EVENT_TYPE_ACCEPT 0
#define EVENT_TYPE_RECV 1
#define EVENT_TYPE_SEND 2

int port;
struct io_uring ring;

struct request{
    int type;
    int socket;
};

int openListeningSocket(int port){
    int socketfd;
    int opt = 1;

    socketfd = socket(AF_INET, SOCK_STREAM, 0);
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
    if(bind(socketfd,(struct sockaddr *)&add, (socklen_t)len) < 0){
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
    io_uring_prep_accept(sqe,socketfd,(struct sockaddr*) addr,len,0);
    return 1;

}

void startServer(int socketfd){
    struct sockaddr_in clientaddr;
    socklen_t addlen = sizeof(clientaddr);



    while(1){
        struct io_uring_cqe* cqe;
        cqe = io_uring_wait_cqe(&ring, &cqe);
        struct request* req = cqe->user_data;

        switch (req->type) {
            case EVENT_TYPE_ACCEPT:
                add_accept_request(socketfd, &clientaddr, &addlen);

        }
    }
}

int main(){
    int socketfd;
    int port;

    printf("Hello! Im the server!!\n");
    io_uring_queue_init(1024,&ring)
    socketfd = openListeningSocket(port);
    startServer(socketfd);


}
