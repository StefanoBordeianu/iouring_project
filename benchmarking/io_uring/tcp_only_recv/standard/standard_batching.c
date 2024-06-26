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
long packetsReceived,bytes_rec,total_bytes,total_packets;

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
//      if(setsockopt(socketfd,SOL_SOCKET,SO_REUSEADDR|SO_REUSEPORT,
//                    &opt,sizeof (opt))){
//            printf("SERVER: Socket options error\n");
//            return -1;
//      }

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

//int openListeningSocket(int port)
//{
//      struct sockaddr_in srv_addr = { };
//      struct sockaddr_in6 srv_addr6 = { };
//      int fd, enable, ret, domain;
//
//      domain = AF_INET;
//
//      fd = socket(domain, SOCK_STREAM, 0);
//      if (fd == -1) {
//            perror("socket()");
//            return -1;
//      }
//
//      enable = 1;
//      ret = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));
//      if (ret < 0) {
//            perror("setsockopt(SO_REUSEADDR)");
//            return -1;
//      }
//
//      srv_addr.sin_family = AF_INET;
//      srv_addr.sin_port = htons(port);
//      srv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
//      ret = bind(fd, (const struct sockaddr *)&srv_addr, sizeof(srv_addr));
//      if (ret < 0) {
//            perror("bind()");
//            return -1;
//      }
//
//      if (listen(fd, 1024) < 0) {
//            perror("listen()");
//            return -1;
//      }
//
//      return fd;
//}


int add_recv_request(int socket, long readlength){
      struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);
      char* buff = malloc(args.size);

      io_uring_prep_recv(sqe,socket, buff,args.size,0);
      io_uring_sqe_set_data(sqe,buff);
      return 1;
}

void startBatchingServer(int sock){
      struct io_uring_cqe* cqe;
      int start = 0;
      unsigned int packets_rec =0;
      int rec;
      int socketfd;
      struct io_uring_sqe* sqe;
      struct sockaddr_in add;
      long len = sizeof(add);


      total_packets = 0;
      total_bytes = 0;
      bytes_rec= 0;
      packetsReceived = 0;

      add.sin_port = htons(args.port);
      add.sin_family = AF_INET;
      add.sin_addr.s_addr = INADDR_ANY;

      sqe = io_uring_get_sqe(&ring);
      io_uring_prep_accept(sqe,sock,(struct sockaddr*)&add, (socklen_t *) &len,0);
      io_uring_submit(&ring);
      io_uring_wait_cqe(&ring,&cqe);
      printf("waited:\n");
      socketfd = cqe->res;
      printf("socket: %d\n",socketfd);

      add_recv_request(socketfd,args.size);


      printf("Entering server loop\n");
      while (1) {
            io_uring_submit_and_wait(&ring,args.batching);
            packets_rec = io_uring_peek_batch_cqe(&ring, &cqe, 1);

            if (!start) {
                  start = 1;
                  alarm(args.duration);
            }

            packetsReceived = packetsReceived + packets_rec;

            for (int i = 0; i < packets_rec; i++) {
                  add_recv_request(socketfd, args.size);
                  struct request* req = io_uring_cqe_get_data(cqe);
                  if(cqe->res <0)
                        printf("error %d\n",cqe->res);

                  bytes_rec += cqe->res;
                  if(cqe->res > 0) {
                        total_bytes += (cqe->res + 20 + 14 + 20 + 20);
                        total_packets++;
                  }

                  //printf("received %ld\n",packetsReceived);
                  free((void*)cqe->user_data);
            }

            io_uring_cq_advance(&ring,packets_rec);
      }
}

void sig_handler(int signum){
      printf("\nReceived: %ld packets of size %d\n",total_packets, args.size);
      printf("\nReceived: %ld events\n",packetsReceived);
      printf("\nReceived: %ld TCP bytes\n",bytes_rec);
      printf("\nReceived: %ld TOTAL bytes\n",total_bytes);

      long speed = packetsReceived/args.duration;
      printf("Speed: %ld packets/second\n", speed);
      printf("Rate: %ld Mb/s\n", (bytes_rec*8)/(args.duration * 1000000));
      printf("Now closing\n\n");
      io_uring_queue_exit(&ring);
      exit(0);
}

int main(int argc, char *argv[]){
      int socketfd;

      parseArgs(argc, argv);
      signal(SIGALRM,sig_handler);

      io_uring_queue_init(32768,&ring,0);
      socketfd = openListeningSocket(args.port);

      printf("starting batching standard server\n");
      startBatchingServer(socketfd);

}
//
// Created by ebpf on 01/10/23.
//
