#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <liburing.h>
#include <time.h>

#define EVENT_TYPE_ACCEPT 0
#define EVENT_TYPE_RECV 1
#define EVENT_TYPE_SEND 2

struct io_uring ring;
long packets_received,bytes_received,total_bytes,total_events;
char start;

struct request{
    int socket;
    int type;
    char* buff;
};

struct args{
    int port;
    int batching;
    int duration;
    int size;
    int debug;
    int test;
};

struct args args;

void parseArgs(int argc, char* argv[]){
      int opt;
      args.port = 2020;
      args.batching = 1;
      args.duration = 10;
      args.test = 0;

      while((opt =getopt(argc,argv,"hs:p:d:b:t")) != -1) {
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
            }
      }
}


int openListeningSocket(int port){
      int socketfd;
      int opt = 1;
      struct sockaddr_in add;

      socketfd = socket(AF_INET, SOCK_STREAM, 0);
      if(socketfd < 0){
            printf("SERVER: Error while creating the socket\n");
            return -1;
      }
      if(setsockopt(socketfd,SOL_SOCKET,SO_REUSEADDR|SO_REUSEPORT,
                    &opt,sizeof (opt))){
            printf("SERVER: Socket options error\n");
            return -1;
      }

      add.sin_port = htons(args.port);
      add.sin_family = AF_INET;
      add.sin_addr.s_addr = INADDR_ANY;

      if (bind(socketfd, (struct sockaddr*)&add,
               sizeof(add))< 0){
            printf("SERVER: Error binding\n");
            return -1;
      }

      if(listen(socketfd,100)){
            printf("SERVER: Error listening\n");
            return -1;
      }
      return socketfd;
}

int add_recv_request(int socket){
      struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);
      char* buff = malloc(args.size);
      struct request* req = malloc(sizeof(struct request));

      req->buff = buff;
      req->type = EVENT_TYPE_RECV;
      req->socket = socket;

      io_uring_prep_recv(sqe,socket, buff,args.size,0);
      io_uring_sqe_set_data(sqe,req);
      return 1;
}

void handle_accept(struct io_uring_cqe* cqe){
      int socket;

      socket = cqe->res;
      printf("Starting receiving on socket %d\n",socket);
      if(!(cqe->flags & IORING_CQE_F_MORE))
            printf("no more accept\n");
      add_recv_request(socket);
}

void handle_recv(struct io_uring_cqe* cqe){
      int socket;
      struct request* req = (struct request*)io_uring_cqe_get_data(cqe);

      if(!start){
            start = 1;
            alarm(args.duration);
      }

      if(cqe->res < 0)
            printf("Error receiving %d\n",cqe->res);

      packets_received++;
      bytes_received += cqe->res;
      total_bytes += cqe->res + 74;

      socket = req->socket;
      add_recv_request(socket);

      free(req->buff);
      free(req);
}

void startBatchingServer(int sock){
      struct io_uring_sqe* sqe;
      struct __kernel_timespec timespec;
      struct request* acc_req = malloc(sizeof(struct request));

      timespec.tv_sec = 0;
      timespec.tv_nsec = 100000000;

      acc_req->type = EVENT_TYPE_ACCEPT;
      acc_req->socket = sock;

      sqe = io_uring_get_sqe(&ring);
      io_uring_prep_multishot_accept(sqe,sock,NULL,NULL,0);
      io_uring_sqe_set_data(sqe,acc_req);
      io_uring_submit(&ring);

      while(1){
            int reaped,head,i;
            struct io_uring_cqe* cqe;
            struct __kernel_timespec *ts = &timespec;

            reaped = io_uring_submit_and_wait_timeout(&ring,&cqe,args.batching,ts,NULL);
            if(reaped < 0)
                  continue;

            //printf("received %d\n",reaped);
            i = 0;
            io_uring_for_each_cqe(&ring,head,cqe){
                  struct request* req = (struct request*)io_uring_cqe_get_data(cqe);
                  switch(req->type){
                        case EVENT_TYPE_ACCEPT:
                              handle_accept(cqe);
                              break;
                        case EVENT_TYPE_RECV:
                              handle_recv(cqe);
                              break;
                  }
                  i++;
            }

            if(i) {
                  total_events += i;
                  io_uring_cq_advance(&ring, i);
            }
      }

}

void sig_handler(int signum){
      printf("\nReceived: %ld packets of size %d\n",packets_received, args.size);
      printf("\nReceived: %ld events\n",total_events);
      printf("\nReceived: %ld TCP bytes\n",bytes_received);
      printf("\nReceived: %ld TOTAL bytes\n",total_bytes);

      long speed = packets_received/args.duration;
      printf("Speed: %ld packets/second\n", speed);
      printf("Rate: %ld Mb/s\n", (bytes_received*8)/(args.duration * 1000000));
      printf("Now closing\n\n");
      io_uring_queue_exit(&ring);
      exit(0);
}

int main(int argc, char *argv[]){
      int socketfd;
      packets_received = 0;
      bytes_received = 0;
      total_bytes = 0;
      total_events = 0;

      parseArgs(argc, argv);
      signal(SIGALRM,sig_handler);

      io_uring_queue_init(32768,&ring,0);
      socketfd = openListeningSocket(args.port);

      printf("starting batching standard server\n");
      startBatchingServer(socketfd);


}
