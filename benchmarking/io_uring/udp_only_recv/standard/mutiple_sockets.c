#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <liburing.h>

struct io_uring ring;
long packetsReceived;
int* socket_arr;

struct request{
    int sock;
    struct msghdr* message;
};

struct args{
    int port;
    int batching;
    int duration;
    int size;
    int debug;
    int test;
    int num_sock;
    int starting_batch;
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
      args.num_sock = 2;
      args.starting_batch = 10;

      while((opt =getopt(argc,argv,"hs:p:d:b:tn:S:")) != -1) {
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
                  case 'n':
                        args.num_sock = atoi(optarg);
                        break;
                  case 'S':
                        args.starting_batch = atoi(optarg);
                        break;
                  case 't':
                        args.test = 1;
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

      req->sock = socket;
      req->message = msg;
      sqe->flags |= IOSQE_IO_LINK;
      io_uring_prep_recvmsg(sqe,socket, msg,0);
      io_uring_sqe_set_data(sqe, req);
      return 1;
}

void startBatchingServer(){
      struct io_uring_cqe* cqe [args.batching];
      int start = 0;
      unsigned int harvested;
      int rec;
      struct request* req;

      for(int i=0;i<args.num_sock;i++){
            for(int j=0;j<args.starting_batch;j++){
                  add_recv_request(socket_arr[i],args.size);
            }
      }

      printf("Entering server loop\n");
      while (1) {
            io_uring_submit_and_wait(&ring,args.batching);
            harvested = io_uring_peek_batch_cqe(&ring, cqe, args.batching);

            if (!start) {
                  start = 1;
                  alarm(args.duration);
            }

            packetsReceived = packetsReceived + harvested;

            for (int i = 0; i < harvested; i++) {
                  req = io_uring_cqe_get_data(cqe[i]);
                  add_recv_request(req->sock, args.size);

                  if(cqe[i]->res < 0)
                        printf("Error %d\n",cqe[i]->res);
                  if(args.debug && (harvested==args.batching))
                        printf("Emptied queue\n");

                  freemsg(req->message);
                  free(req);
            }

            io_uring_cq_advance(&ring,harvested);
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

      parseArgs(argc, argv);
      signal(SIGALRM,sig_handler);

      socket_arr = malloc(sizeof(int)*args.num_sock);

      io_uring_queue_init(32768,&ring,0);
      for(int i=0;i<args.num_sock;i++){
            socket_arr[i] = openListeningSocket(args.port+i);
      }

      printf("starting batching standard server\n");
      startBatchingServer();

}
//
// Created by ebpf on 01/10/23.
//
