#include <stdio.h>
#include <linux/io_uring.h>
#include <liburing.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>

#define QUEUE_DEPTH 1024
#define EVENT_TYPE_ACCEPT 0
#define EVENT_TYPE_RECEIVE 1
#define EVENT_TYPE_SEND 2

struct request{
    int type;
};

struct io_uring ring;
struct sockaddr_in add;
long len = sizeof(add);

/*SETUP THE LISTENING SOCKET
 * Function initializes the socket
 * and puts it in the listening state */
int setup_listening_socket(int port){
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



void receive_messages(int socketfd){
    struct io_uring_cqe* cqe;
    char recbuffer [50];
    char* toSend = "peepo, Im here!";

    int ret = io_uring_wait_cqe(&ring, &cqe);
    struct request* req = (struct request*) cqe->user_data;
    if(ret<0){
        printf("Error in the waiting for accept cqe\n");
        exit(-1);
    }
    if(req->type != EVENT_TYPE_ACCEPT){
        printf("received not an accept as first\n");
        exit(-1);
    }

    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    struct request *req2 = malloc(sizeof (struct request));
    req2->type = EVENT_TYPE_RECEIVE;
    int clientsocket = cqe->res;
    io_uring_prep_recv(sqe,clientsocket,recbuffer,50,0);
    io_uring_sqe_set_data(sqe,req2);
    io_uring_cqe_seen(&ring, cqe);
    io_uring_submit(&ring);
    ret = io_uring_wait_cqe(&ring, &cqe);
    if(ret != 0){
        printf("failed retrieving cqe 2\n");
        int error = errno;
        printf("CLIENT: Error while receiving, ERROR=%d\n",error);
        exit(-1);
    }
    req = (struct request*) cqe->user_data;
    if(req->type != EVENT_TYPE_RECEIVE){
        printf("received not an recv as second, type: %d\n",req->type);
        exit(-1);
    }
    printf("Received string: %s",recbuffer);

    sqe = io_uring_get_sqe(&ring);
    req = malloc(sizeof(struct request));
    req->type = EVENT_TYPE_SEND;
    io_uring_prep_send(sqe,clientsocket,toSend,
                       (size_t) strlen(toSend),0);
    io_uring_sqe_set_data(sqe, req);
    io_uring_cqe_seen(&ring, cqe);
    io_uring_submit(&ring);
    ret = io_uring_wait_cqe(&ring, &cqe);
    req = (struct request*) cqe->user_data;
    if(ret<0){
        printf("Error in the waiting for send cqe\n");
        exit(-1);
    }
    if(req->type != EVENT_TYPE_SEND){
        printf("received not an send as third, type: %d\n",req->type);
        exit(-1);
    }
    printf("sent %d bytes\n",cqe->res);
}


void init(){
    int socketfd;
    struct io_uring_sqe* sqe;

    socketfd = setup_listening_socket(8080);
    io_uring_queue_init(QUEUE_DEPTH, &ring, 0);
    sqe = io_uring_get_sqe(&ring);
    if(sqe < 0){
        printf("SERVER: error while retrieving the sqe\n");
        return;
    }
    struct request* req = (struct request*) malloc(sizeof(struct request));
    io_uring_prep_accept(sqe,socketfd,(struct sockaddr*)&add,
                         (socklen_t*)&len,0);
    io_uring_sqe_set_data(sqe, req);
    io_uring_submit(&ring);

    receive_messages(socketfd);

}

int main(){

    printf("Hello im the server :D\n");
    init();
    printf("EVERYTHING IS DONE IM NOW CLOSING\n");
    return 1;
}