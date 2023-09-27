#include <stdio.h>
#include <linux/io_uring.h>
#include <liburing.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>

#define QUEUE_DEPTH 1024

struct io_uring ring;

int setup_listening_socket(int port){

    int socketfd;
    struct sockaddr_in add;
    long len = sizeof(add);
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
    add.sin_addr.s_addr = htonl(INADDR_ANY);
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


void init(){

    int socketfd;
    struct io_uring_sqe* sqe;

    socketfd = setup_listening_socket(8080);

    io_uring_queue_init(QUEUE_DEPTH, &ring, 0);
    sqe = io_uring_get_sqe(&ring);
    if(sqe){
        printf("SERVER: error while retrieving the sqe\n");
        return;
    }



}

int main(){

    printf("Hello im the server :D\n");
    init();
    return 1;
}