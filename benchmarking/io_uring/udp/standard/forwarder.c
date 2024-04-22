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
    int test;
    int coop;
    int async;
    int single;
    int defer;
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
      args.test = 0;
      args.coop = 0;
      args.async = 0;
      args.single = 0;
      args.defer = 0;

      while((opt =getopt(argc,argv,"hs:p:d:b:tcagf")) != -1) {
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
                  case 't':
                        args.test = 1;
                        break;
                  case 'c':
                        args.coop = 1;
                        break;
                  case 'a':
                        args.async = 1;
                        break;
                  case 'g':
                        args.single = 1;
                        break;
                  case 'f':
                        args.defer = 1;
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
      struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);
      if(sqe == NULL)
            printf("ERROR while getting the sqe\n");

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
      if(args.async)
            io_uring_sqe_set_flags(sqe,IOSQE_ASYNC);
      return 1;
}

void startBatchingServer(int socketfd){
      struct io_uring_cqe* cqe [args.batching];
      int start = 0;
      unsigned int packets_rec;
      int rec;

      for(int i=0;i<args.batching;i++)
            add_recv_request(socketfd,args.size);


      printf("Entering server loop\n");
      while (1) {
            io_uring_submit_and_wait(&ring,args.batching);
            packets_rec = io_uring_peek_batch_cqe(&ring, cqe, args.batching);

            if (!start) {
                  start = 1;
                  alarm(args.duration);
            }

            packetsReceived = packetsReceived + packets_rec;

            for (int i = 0; i < packets_rec; i++) {
                  add_recv_request(socketfd, args.size);
                  struct request* req = io_uring_cqe_get_data(cqe[i]);

                  if(args.debug && (packets_rec==args.batching))
                        printf("Emptied queue\n");

                  freemsg(req->message);
                  free(req);
            }

            io_uring_cq_advance(&ring,packets_rec);
      }
}

void sig_handler(int signum){
      printf("\nReceived: %ld packets of size %d\n",packetsReceived, args.size);
      long speed = packetsReceived/args.duration;
      long bytes_rec = packetsReceived*args.size;
      printf("Speed: %ld packets/second\n", speed);
      printf("Rate: %ld Mb/s\n", (bytes_rec*8)/(args.duration * 1000000));
      printf("Now closing\n\n");
      if(args.test) {
            FILE *file = fopen("batching_res.txt", "a");
            fprintf(file, "%ld\n", speed);
            fprintf(file, "%f\n", ((double) (bytes_rec * 8)) / (args.duration * 1000000));
            fclose(file);
      }
      io_uring_queue_exit(&ring);
      exit(0);
}

int main(int argc, char *argv[]){
      int socketfd;
      struct io_uring_params params = {};

      parseArgs(argc, argv);
      signal(SIGALRM,sig_handler);

      if(args.coop)
            params.flags |= IORING_SETUP_COOP_TASKRUN;
      if(args.single)
            params.flags |= IORING_SETUP_SINGLE_ISSUER;
      if(args.defer)
            params.flags |= IORING_SETUP_DEFER_TASKRUN;


      io_uring_queue_init_params(1024,&ring,&params);
      socketfd = openListeningSocket(args.port);

      printf("starting batching standard server\n");
      startBatchingServer(socketfd);


}
//
// Created by ebpf on 01/10/23.
//
