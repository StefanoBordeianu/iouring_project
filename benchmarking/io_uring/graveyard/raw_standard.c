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
#include <linux/ip.h>

#define EVENT_TYPE_SEND 1
#define EVENT_TYPE_RECV 2

int port = 2020;
int batching = 1;
int test = 0;
int duration = 10 ;
int coop = 0;
int async = 0;
int single = 0;
int defer = 0;
int size = 64;
int initial_count = 64;
int ring_entries = 1024;
int fixed_file = 0;
int sq_poll = 0;
int napi = 0;
int napi_timeout = 0;

int iter = 0;
struct io_uring* ring;
int start = 0;
long packets_received = 0;
long packets_sent = 0;
long total_events = 0;
int fixed_files[10];
struct request* requests;

struct request{
    struct msghdr* msg;
    int type;
    int socket;
    int index;
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

      while((opt =getopt(argc,argv,"hs:p:d:b:TACSDi:r:FPNn:")) != -1) {
            switch (opt) {
                  case 'p':
                        port = atoi(optarg);
                        break;
                  case 'b':
                        batching =  atoi(optarg);
                        break;
                  case 'd':
                        duration = atoi(optarg);
                        break;
                  case 's':
                        size = atoi(optarg);
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
                  case 'n':
                        napi_timeout = atoi(optarg);
                        break;
                  case 'h':
                        print_usage();
                        return -1;
            }
      }
      return 1;
}

int create_socket(){
      int socketfd;
      int opt = 1;
      struct sockaddr_in add;

      socketfd = socket(AF_INET, SOCK_RAW, IPPROTO_UDP);
      if(socketfd < 0){
            printf("SERVER: Error while creating the socket\n");
            exit(-1);
      }
      if(setsockopt(socketfd,SOL_SOCKET,SO_REUSEADDR|SO_REUSEPORT,
                    &opt,sizeof (opt))){
            printf("SERVER: Socket options error\n");
            exit(-1);
      }
      if(setsockopt(socketfd, IPPROTO_IP, IP_HDRINCL, &opt, sizeof(opt))<0){
            perror("IP header option\n");
            return 0;
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

void handle_buffer(char* buffer,struct sockaddr_in* res){
      struct iphdr* iphdr = (struct iphdr*) buffer;
      res->sin_addr.s_addr = iphdr->saddr;
      iphdr->saddr = iphdr->daddr;
      iphdr->daddr = res->sin_addr.s_addr;

      res->sin_port = 0;
      res->sin_family = AF_INET;

}

void add_send(struct request* req){
      struct msghdr* msghdr;
      struct io_uring_sqe* sqe;
      int socketfd;

      socketfd = req->socket;
      msghdr = req->msg;
      handle_buffer(msghdr->msg_iov->iov_base,msghdr->msg_name);
      //free(msghdr->msg_name);

      req->type = EVENT_TYPE_SEND;
      sqe = io_uring_get_sqe(ring);
      if(sqe == NULL)
            printf("ERROR while getting the sqe\n");

      io_uring_prep_sendto(sqe,socketfd,msghdr->msg_iov->iov_base,size,0,msghdr->msg_name,sizeof(struct sockaddr_in));
      io_uring_sqe_set_data(sqe, req);
      if(fixed_file)
            io_uring_sqe_set_flags(sqe,IOSQE_FIXED_FILE);
      if(async)
            io_uring_sqe_set_flags(sqe,IOSQE_ASYNC);
}

void add_starting_receive(int socketfd,int index){
      struct msghdr* msghdr;
      struct sockaddr_in* src_add;
      struct iovec* iov;
      struct request* req;
      struct io_uring_sqe* sqe;

      req = &requests[index];
      iov = malloc(sizeof(struct iovec));
      msghdr = malloc(sizeof(struct msghdr));
      src_add = malloc(sizeof(struct sockaddr_in));
      //memset(req,0,sizeof(*req));
      memset(iov,0,sizeof(*iov));
      memset(msghdr,0,sizeof(*msghdr));
      memset(src_add,0,sizeof(*src_add));

      sqe = io_uring_get_sqe(ring);
      if(sqe == NULL)
            printf("ERROR while getting the sqe\n");

      iov->iov_len = size;
      iov->iov_base = malloc(size);
      memset(iov->iov_base,0,size);
      
      msghdr->msg_name = src_add;
      msghdr->msg_namelen = sizeof(struct sockaddr_in);
      msghdr->msg_iov = iov;
      msghdr->msg_iovlen = 1;

      req->type = EVENT_TYPE_RECV;
      req->msg = msghdr;
      req->socket = socketfd;

      io_uring_prep_recvmsg(sqe,socketfd, msghdr,0);
      io_uring_sqe_set_data(sqe, req);
      if(fixed_file)
            io_uring_sqe_set_flags(sqe,IOSQE_FIXED_FILE);
      if(async)
            io_uring_sqe_set_flags(sqe,IOSQE_ASYNC);
}

void add_receive(struct request* req){
      struct io_uring_sqe* sqe;

      sqe = io_uring_get_sqe(ring);
      if(sqe == NULL)
            printf("ERROR while getting the sqe\n");

      memset(req->msg->msg_iov->iov_base,0,size);
      memset(req->msg->msg_name,0,sizeof(struct sockaddr_in));

      req->type = EVENT_TYPE_RECV;

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
            printf("Error data:  %d index,   %d iteration\n",req->index,iter);
      }

      packets_sent++;
      //freemsg(req->msg);
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

      packets_received++;
      add_send(req);
}

void initialize_local(){
      requests = malloc(sizeof(struct request)*initial_count);

      for(int i=0;i<initial_count;i++){
            requests[i].index = i;
      }
}

void start_loop(int socketfd){
      struct __kernel_timespec timespec;
      timespec.tv_sec = 0;
      timespec.tv_nsec = 100000000;

      initialize_local();

      for(int i=0;i<initial_count;i++){
            if(fixed_file==1)
                  add_starting_receive(0,i);
            else
                  add_starting_receive(socketfd,i);
      }

      while(1){
            int reaped,head,i;
            struct io_uring_cqe* cqe;
            struct __kernel_timespec *ts = &timespec;

            reaped = io_uring_submit_and_wait_timeout(ring,&cqe,batching,ts,NULL);
            if(reaped < 0)
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
            iter++;
      }
}

void sig_handler(int signum){
      printf("\nReceived: %ld packets of size %d\n",packets_received, size);
      printf("\nSent: %ld packets of size %d\n",packets_sent, size);
      printf("\nProcessed: %ld events\n",total_events);

      long speed = packets_received/duration;
      printf("Speed: %ld packets/second\n", speed);
      printf("Now closing\n\n");
      io_uring_queue_exit(ring);
      free(ring);
      exit(0);
}

int main(int argc, char* argv[]){
      int socketfd,ret;
      struct io_uring_params params = {};
      ring = malloc(sizeof(struct io_uring));

      if(parse_arguments(argc,argv)<0)
            return -1;

      signal(SIGALRM,sig_handler);
      socketfd = create_socket();

      if(coop)
            params.flags |= IORING_SETUP_COOP_TASKRUN;
      if(single)
            params.flags |= IORING_SETUP_SINGLE_ISSUER;
      if(defer)
            params.flags |= IORING_SETUP_DEFER_TASKRUN;
      if(sq_poll) {
            params.flags |= IORING_SETUP_SQPOLL;
            params.sq_thread_idle = 10000;
      }

      if(io_uring_queue_init_params(ring_entries,ring,&params)<0){
            printf("Init ring error\n");
            exit(-1);
      }

      if(fixed_file){
            fixed_files[0] = socketfd;
            if(io_uring_register_files(ring,fixed_files,1)<0){
                  printf("Register file error\n");
                  exit(-1);
            }
      }

      if (napi) {
            struct io_uring_napi n = {
                    .prefer_busy_poll = napi > 1 ? 1 : 0,
                    .busy_poll_to = napi_timeout,
            };

            ret = io_uring_register_napi(ring, &n);
            if (ret) {
                  fprintf(stderr, "io_uring_register_napi: %d\n", ret);
                  if (ret != -EINVAL)
                        return 1;
                  fprintf(stderr, "NAPI not available, turned off\n");
            }
      }

      start_loop(socketfd);
}
