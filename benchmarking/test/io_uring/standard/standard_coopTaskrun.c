#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <liburing.h>


#define EVENT_TYPE_ACCEPT 0
#define EVENT_TYPE_RECV 1
#define EVENT_TYPE_SEND 2

struct io_uring ring;
long packetsReceived;
long bytes_rec;

struct request{
    int type;
    struct msghdr* message;
};

struct args{
    int port;
    int duration;
    int size;
    int debug;
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
      args.duration = 10;

      while((opt =getopt(argc,argv,"hs:p:d:b:")) != -1) {
            switch (opt) {
                  case 'p':
                        args.port = atoi(optarg);
                        break;
                  case 'd':
                        args.duration = atoi(optarg);
                        break;
                  case 's':
                        args.size = atoi(optarg);
                        break;
            }
      }
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
      printf("1\n");
      struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);

      struct request* req = malloc(sizeof(struct request));

      struct msghdr* msg = malloc(sizeof(struct msghdr));
      struct iovec* iov = malloc(sizeof(struct iovec));

      printf("A\n");
      memset(msg, 0, sizeof(struct msghdr));
      memset(iov,0,sizeof(struct iovec));
      iov->iov_base = malloc(readlength);
      iov->iov_len = readlength;
      msg->msg_name = NULL;
      msg->msg_namelen = 0;
      msg->msg_iov = iov;
      msg->msg_iovlen = 1;

      printf("B\n");
      req->type = EVENT_TYPE_RECV;
      req->message = msg;
      printf("C\n");
      io_uring_prep_recvmsg(sqe,socket, msg,0);
      printf("D\n");
      io_uring_sqe_set_data(sqe, req);
      return 1;
}

void startServer(int socketfd){
      struct io_uring_cqe* cqe;
      int start = 0;
      add_recv_request(socketfd,args.size);
      printf("before submitting\n");
      io_uring_submit(&ring);

      printf("Entering server loop\n");
      while(1){
            if(io_uring_wait_cqe(&ring, &cqe)){
                  printf("ERROR WAITING\n");
                  exit(-1);
            }
            struct request* req = io_uring_cqe_get_data(cqe);

            if(!start){
                  start = 1;
                  alarm(args.duration);
                  printf("alarm set\n");
            }
            packetsReceived++;
            bytes_rec += cqe->res;
            add_recv_request(socketfd,args.size);
            io_uring_submit(&ring);
            freemsg(req->message);
            free(req);

            io_uring_cqe_seen(&ring, cqe);
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
      struct io_uring_params params;

      parseArgs(argc, argv);
      signal(SIGALRM,sig_handler);

      memset(&params, 0, sizeof(params));
      params.flags |= IORING_SETUP_COOP_TASKRUN;
      params.flags |= IORING_SETUP_TASKRUN_FLAG;

      io_uring_queue_init_params(32768,&ring,&params);
      socketfd = openListeningSocket(args.port);

      printf("starting coop standard server\n");
      startServer(socketfd);

}
//
// Created by ebpf on 01/10/23.
//
