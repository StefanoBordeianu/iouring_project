#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <liburing/io_uring.h>
#include <liburing.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <signal.h>
#include <unistd.h>


#define EVENT_TYPE_SEND 1
#define EVENT_TYPE_RECV 2

int starting_port = 2020;
int batching = 1;
int test = 0;
int duration = 10 ;
int coop = 0;
int async = 0;
int single = 0;
int defer = 0;
int size = 64;
int initial_count = 16;
int ring_entries = 1024;
int fixed_file = 0;
int sq_poll = 0;
int napi = 0;
int napi_timeout = 0;
int number_of_sockets = 1;
int sink = 0;
int report = 0;
int sq_affinity = 0;
int batch_info = 0;
int waitcq=0;

struct io_uring* ring;
int start = 0;
long* pkts_recv_per_socket;
long* pkts_sent_per_socket;
long total_events = 0;
int fixed_files[10];

struct request{
    struct msghdr* msg;
    int type;
    int socket;
    int socket_index;
};

void print_usage(){
      printf("-p  port\n-b  receiving batching size\n-d  test duration\n-s  packet size\n"
             "-i  initial request count in the ring\n-r  ring size\n-T  is testing (currently useless)\n"
             "-A  enable async sqe option\n-C  enable coop option\n-S  enable single issue option\n"
             "-d  enable defer taskrun option\n-F  enable fixed file\n-P  enable SQpoll\n"
             "-N  enable napi\n-n  napi timeout");
}

void freemsg(struct msghdr * msg){
      free(msg->msg_name);
      free(msg->msg_iov->iov_base);
      free(msg->msg_iov);
      free(msg);
}

int parse_arguments(int argc, char* argv[]){
      int opt;

      while((opt =getopt(argc,argv,"hs:p:Bd:b:a:TACSDi:r:FPNn:k:KRw")) != -1) {
            switch (opt) {
                  case 'p':
                        starting_port = atoi(optarg);
                        break;
                  case 'b':
                        batching =  atoi(optarg);
                        break;
                  case 'd':
                        duration = atoi(optarg);
                        break;
                  case 's':
                        size = atoi(optarg)-56;
                        break;
                  case 'T':
                        test = 1;
                        break;
                  case 'C':
                        coop = 1;
                        break;
                  case 'A':
                        async = 1;
                        break;
                  case 'a':
                        sq_affinity = atoi(optarg);
                        break;
                  case 'S':
                        single = 1;
                        break;
                  case 'D':
                        defer = 1;
                        break;
                  case 'i':
                        initial_count = atoi(optarg);
                        break;
                  case 'r':
                        ring_entries = atoi(optarg);
                        break;
                  case 'F':
                        fixed_file = 1;
                        break;
                  case 'P':
                        fixed_file = 1;
                        sq_poll = 1;
                        break;
                  case 'N':
                        napi = 1;
                        break;
                  case 'w':
                        waitcq=1;
                        break;
                  case 'n':
                        napi_timeout = atoi(optarg);
                        break;
                  case 'k':
                        number_of_sockets = atoi(optarg);
                        break;
                  case 'K':
                        sink = 1;
                        break;
                  case 'R':
                        report = 1;
                        break;
                  case 'B':
                        batch_info = 1;
                        break;
                  case 'h':
                        print_usage();
                        return -1;
            }
      }
      return 1;
}

int create_socket(int port){
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

void add_send(struct request* req){
      struct msghdr* msghdr;
      struct io_uring_sqe* sqe;
      int socketfd;

      socketfd = req->socket;
      msghdr = req->msg;
      req->type = EVENT_TYPE_SEND;
      sqe = io_uring_get_sqe(ring);
      if(sqe == NULL)
            printf("ERROR while getting the sqe\n");

      io_uring_prep_sendmsg(sqe,socketfd,msghdr,0);
      io_uring_sqe_set_data(sqe, req);
      if(fixed_file)
            io_uring_sqe_set_flags(sqe,IOSQE_FIXED_FILE);
      if(async)
            io_uring_sqe_set_flags(sqe,IOSQE_ASYNC);
}

void add_starting_receive(int socket_index,int* sockets){
      struct msghdr* msghdr;
      struct sockaddr_in* src_add;
      struct iovec* iov;
      struct request* req;
      struct io_uring_sqe* sqe;

      req = malloc(sizeof(struct request));
      iov = malloc(sizeof(struct iovec));
      msghdr = malloc(sizeof(struct msghdr));
      src_add = malloc(sizeof(struct sockaddr_in));
      sqe = io_uring_get_sqe(ring);
      if(sqe == NULL)
            printf("ERROR while getting the sqe\n");

      iov->iov_len = 1500;
      iov->iov_base = malloc(1500);
      
      msghdr->msg_name = src_add;
      msghdr->msg_namelen = sizeof(struct sockaddr_in);
      msghdr->msg_iov = iov;
      msghdr->msg_iovlen = 1;

      req->type = EVENT_TYPE_RECV;
      req->msg = msghdr;
      req->socket_index = socket_index;


      if(fixed_file) {
            io_uring_prep_recvmsg(sqe, socket_index, msghdr, 0);
            io_uring_sqe_set_flags(sqe, IOSQE_FIXED_FILE);
            req->socket = socket_index;
      }
      else {
            io_uring_prep_recvmsg(sqe, sockets[socket_index], msghdr, 0);
            req->socket = sockets[socket_index];
      }

      io_uring_sqe_set_data(sqe, req);
      if(async)
            io_uring_sqe_set_flags(sqe,IOSQE_ASYNC);
}

void add_receive(struct request* req){
      struct io_uring_sqe* sqe;

      sqe = io_uring_get_sqe(ring);
      if(sqe == NULL)
            printf("ERROR while getting the sqe\n");

      req->type = EVENT_TYPE_RECV;
      req->msg->msg_iov->iov_len = 1500;

      io_uring_prep_recvmsg(sqe,req->socket, req->msg,0);
      io_uring_sqe_set_data(sqe, req);
      if(fixed_file)
            io_uring_sqe_set_flags(sqe,IOSQE_FIXED_FILE);
      if(async)
            io_uring_sqe_set_flags(sqe,IOSQE_ASYNC);
}

void handle_send(struct io_uring_cqe* cqe){
      struct request* req = (struct request*)io_uring_cqe_get_data(cqe);

      if(cqe->res < 0){
            printf("error on send,  number:%d\n",cqe->res);
      }

      pkts_sent_per_socket[req->socket_index]++;
      add_receive(req);
}

void handle_recv(struct io_uring_cqe* cqe){
      struct request* req = (struct request*)io_uring_cqe_get_data(cqe);

      if(!start){
            start = 1;
            alarm(duration);
            printf("alarm set\n");
      }
      if(cqe->res < 0){
            printf("error on receive,  number:%d\n",cqe->res);
      }

      pkts_recv_per_socket[req->socket_index]++;

      req->msg->msg_iov->iov_len = cqe->res;
      if(!sink)
            add_send(req);
      else
            add_receive(req);
}

void start_loop(int* sockets){
      struct __kernel_timespec timespec;
      timespec.tv_sec = 0;
      timespec.tv_nsec = 100000000;

      for(int i=0;i<number_of_sockets;i++){
            for (int j = 0; j < initial_count; j++) {
                  add_starting_receive(i,sockets);
            }
      }

      while(1){
            int r,head,i;
            struct io_uring_cqe* cqe;
            struct __kernel_timespec *ts = &timespec;

            if(sq_poll==0 && waitcq==1)
                  r = io_uring_submit_and_wait_timeout(ring,&cqe,batching,ts,NULL);
            else{
                  io_uring_submit(ring);
                  r = (int) io_uring_peek_batch_cqe(ring,&cqe,batching);
                  if(r==batching && batch_info)
                        printf("Batching filled\n");
            }

            if(r==0)
                  continue;
            i=0;
            io_uring_for_each_cqe(ring,head,cqe){
                  struct request* req = (struct request*)io_uring_cqe_get_data(cqe);
                  switch(req->type){
                        case EVENT_TYPE_SEND:
                              handle_send(cqe);
                              break;
                        case EVENT_TYPE_RECV:
                              handle_recv(cqe);
                              break;
                  }
                  i++;
            }

            if(i) {
                  total_events += i;
                  io_uring_cq_advance(ring, i);
            }
      }
}

void sig_handler(int signum){
      if(report)
            for(int i=0;i<number_of_sockets;i++){
                  printf("SOCKET index %d\n",i);
                  if(!sink) {
                        printf("Received: %ld packets\n", pkts_recv_per_socket[i]);
                        printf("Sent: %ld packets\n", pkts_sent_per_socket[i]);
                  }
                  long speed = pkts_recv_per_socket[i]/duration;
                  printf("Speed: %ld packets/second\n\n", speed);
            }

      printf("\nProcessed: %ld events\n",total_events);
      printf("Now closing\n\n");
      io_uring_queue_exit(ring);
      free(pkts_recv_per_socket);
      free(pkts_sent_per_socket);
      free(ring);
      exit(0);
}

int main(int argc, char* argv[]){
      int ret;
      struct io_uring_params params = {};
      ring = malloc(sizeof(struct io_uring));

      if(parse_arguments(argc,argv)<0)
            return -1;

      signal(SIGALRM,sig_handler);
      int sockets[number_of_sockets];
      pkts_recv_per_socket = malloc(sizeof(long)*number_of_sockets);
      pkts_sent_per_socket = malloc(sizeof(long)*number_of_sockets);


      for(int i=0;i<number_of_sockets;i++)
            sockets[i] = create_socket(starting_port+i);

      //params.flags |= IORING_FEAT_NATIVE_WORKERS;

      if(coop) {
            params.flags |= IORING_SETUP_COOP_TASKRUN;
      }
      if(single)
            params.flags |= IORING_SETUP_SINGLE_ISSUER;
      if(defer)
            params.flags |= IORING_SETUP_DEFER_TASKRUN;
      if(sq_poll) {
            if(sq_affinity!=0) {
                  params.flags |= IORING_SETUP_SQ_AFF;
                  params.sq_thread_cpu = sq_affinity;
            }
            params.flags |= IORING_SETUP_SQPOLL;
            params.sq_thread_idle = 10000;
      }

      if(io_uring_queue_init_params(ring_entries,ring,&params)<0){
            printf("Init ring error\n");
            exit(-1);
      }

      if(fixed_file){
            if(io_uring_register_files(ring,sockets,number_of_sockets)<0){
                  printf("Register file error\n");
                  exit(-1);
            }
      }

      start_loop(sockets);
}
