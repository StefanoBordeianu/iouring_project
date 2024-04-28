#include <stdio.h>
#include <linux/io_uring.h>
#include <liburing.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/tcp.h>


#define EVENT_TYPE_ACCEPT 0
#define EVENT_TYPE_RECV 1
#define EVENT_TYPE_SEND 2

struct io_uring ring;
int socketfd;

struct request{
    int type;
    int socket;
    char* buff;
};

void fatal_error(char* error){
    perror(error);
    exit(-1);
}

void createSocket(int argc, char* argv[]){
    int port, opt=1;
    long bytes_sent, rec;
    struct sockaddr_in addr;
    char* ip;

    if(argc==1){
        port = 8080;
        char* ip_temp = "127.0.0.1";
        ip = ip_temp;
    }
    else if (argc==3){
        port = strtol(argv[2],NULL,10);
        ip = argv[1];
    }
    else{
        printf("NO ARGUMENTS FOR LOCAL,  IP,PORT OTHERWISE\n");
        exit(-1);
    }
    addr.sin_port = htons(port);
    addr.sin_family = AF_INET;
    if(inet_pton(AF_INET,ip, &addr.sin_addr) <= 0)
        fatal_error("Error setting the address\n");

    socketfd = socket(AF_INET, SOCK_STREAM, 0);
    if(socketfd < 0)
        fatal_error("socket creation\n");
    if(setsockopt(socketfd,SOL_SOCKET,TCP_NODELAY|SO_REUSEADDR|SO_REUSEPORT,
                  &opt,sizeof (opt)))
        fatal_error("sockopt\n");
    if(connect(socketfd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        fatal_error("Connecting to socket\n");
}

int add_send_request(char* toSend){
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);
    struct request* req = malloc(sizeof(struct request));
    req->type = EVENT_TYPE_SEND;
    req->socket = socketfd;
    io_uring_prep_send(sqe, socketfd, toSend, strlen(toSend),0);
    io_uring_sqe_set_data(sqe, req);

    return 1;
}

int add_recv_request(long readlength){
    struct io_uring_sqe* sqe;
    struct request* req = malloc(sizeof(struct request));
    sqe = io_uring_get_sqe(&ring);
    req->buff = malloc(readlength);
    req->socket = socketfd;
    req->type = EVENT_TYPE_RECV;
    memset(req->buff, 0, readlength);
    io_uring_prep_recv(sqe,socketfd,req->buff,readlength,0);
    io_uring_sqe_set_data(sqe, req);
    io_uring_submit(&ring);
    return 1;
}

void startSending(){
    long bytes_sent, bytes_received;

    while (1){
        int packetsToSend;
        printf("How many packets do you wanna send?\n");
        scanf("%d",&packetsToSend);
        for(int i=0; i<packetsToSend; i++){
            char toSend [50];
            sprintf(toSend, "Packet number: %d", i);
            add_send_request(toSend);
            add_recv_request(1024);
            io_uring_submit(&ring);
        }

        for (int i=0; i<(2*packetsToSend); i++) {
            struct io_uring_cqe* cqe;
            //if(io_uring_peek_cqe(&ring, &cqe))
            //   break;
            io_uring_wait_cqe(&ring, &cqe);
            struct request* req = (struct request*)cqe->user_data;
            if(req->type == EVENT_TYPE_RECV){
                printf("received packet: %s\n",req->buff);
                free(req->buff);
            }
            else
                printf("sent packet\n");
            free(req);
            io_uring_cqe_seen(&ring,cqe);
        }
    }
}

void sigint_handler(int signo) {
    printf("^C pressed. Shutting down.\n");
    io_uring_queue_exit(&ring);
    close(socketfd);
    exit(0);
}

int main(int argc, char *argv[]){

    signal(SIGINT, sigint_handler);
    printf("Hello, im the client :D!\n");
    io_uring_queue_init(32768,&ring,0);
    createSocket(argc, argv);
    startSending();
}