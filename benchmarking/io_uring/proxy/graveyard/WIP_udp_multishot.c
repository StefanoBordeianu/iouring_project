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
#include <sys/mman.h>


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
int fixed_file = 1;
int sq_poll = 0;
int number_of_buffers = 2048;

struct io_uring* ring;
int start = 0;
long packets_received = 0;
long packets_sent = 0;
long total_events = 0;
int fixed_files[10];
char** buffers;
int grp_id = 40;
struct msghdr* send_msgs;
struct iovec* send_iovecs;
struct io_uring_buf_ring* buff_ring;
struct msghdr msg;

struct request{
    struct msghdr* msg;
    int group;
    int buffer_id;
    int type;
    int socket;
};

void print_usage(){
      printf("-p  port\n-b  batching size\n-d  test duration\n-s  packet size\n-i  initial request count\n"
             "-r  ring size\n-T  is testing\n-A  async sqe option\n-C  coop option\n-S  single issue option\n"
             "-d  defer taskrun option\n-F  fixed file\n-P  SQpoll\n");
}

void freemsg(struct msghdr * msghdr){
      free(msghdr->msg_name);
      free(msghdr->msg_iov->iov_base);
      free(msghdr->msg_iov);
      free(msghdr);
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

      socketfd = socket(AF_INET, SOCK_DGRAM, 0);
      if(socketfd < 0){
            printf("SERVER: Error while creating the socket\n");
            exit(-1);
      }
//      if(setsockopt(socketfd,SOL_SOCKET,SO_REUSEADDR|SO_REUSEPORT,
//                    &opt,sizeof (opt))){
//            printf("SERVER: Socket options error\n");
//            exit(-1);
//      }

      add.sin_port = htons(port);
      add.sin_family = AF_INET;
      add.sin_addr.s_addr = INADDR_ANY;
      if(bind(socketfd,(struct sockaddr *)&add, sizeof(add)) < 0){
            perror("bind()");
            exit(-1);
      }
      return socketfd;
}

struct io_uring_buf_ring* init_buff_ring(){
      struct io_uring_buf_reg reg = {};
      struct io_uring_buf_ring *br;
      int i,bgid,ret;
      long page_size = sysconf(_SC_PAGESIZE);
      bgid = grp_id;

//      printf("SIZE: %ld\nSCpagesize:  %ld\n",
//             (number_of_buffers * sizeof(struct io_uring_buf)),page_size);
//      if (posix_memalign((void **) &br, page_size,number_of_buffers * sizeof(struct io_uring_buf))){
//            printf("1st Posix\n");
//            return NULL;
//      }
      //printf("1st posix passed\n");
      br = io_uring_setup_buf_ring(ring,number_of_buffers,bgid,0,&ret);
      if (ret) {
            fprintf(stderr, "buf_ring init failed: %d\n",-ret);
            exit(1);
      }
      printf("setup buff ring passed\n");
      if(br == NULL){
            printf("setup buff ring\n");
            return NULL;
      }

      //TODO: bugged, for some reason it will segfault
//      if(posix_memalign((void**)buffers, page_size, number_of_buffers*(size+64))){
//            printf("2nd Posix\n");
//            return NULL;
//      }

      buffers = malloc(sizeof(char*)*number_of_buffers);

      for (i = 0; i < number_of_buffers; i++) {
            int mask = io_uring_buf_ring_mask(number_of_buffers);
            buffers[i] = malloc( 2000);
            io_uring_buf_ring_add(br, buffers[i], 2000, i,mask,i);
      }
      printf("ring added passed\n");

      io_uring_buf_ring_advance(br, number_of_buffers);
      printf("buffer ring created\n");
      return br;
}

//int setup_buffer_pool(struct ctx *ctx)
//{
//      int ret, i;
//      void *mapped;
//      struct io_uring_buf_reg reg = { .ring_addr = 0,
//              .ring_entries = number_of_buffers,
//              .bgid = 0 };
//      unsigned long buf_ring_size;
//      int buffer_size = size +64;
//
//      buf_ring_size = (sizeof(struct io_uring_buf) + buffer_size) * number_of_buffers;
//      mapped = mmap(NULL, buf_ring_size, PROT_READ | PROT_WRITE,
//                    MAP_ANONYMOUS | MAP_PRIVATE, 0, 0);
//      if (mapped == MAP_FAILED) {
//            fprintf(stderr, "buf_ring mmap: %s\n", strerror(errno));
//            return -1;
//      }
//      buff_ring = (struct io_uring_buf_ring *)mapped;
//
//      io_uring_buf_ring_init(buff_ring);
//
//      reg = (struct io_uring_buf_reg) {
//              .ring_addr = (unsigned long)buff_ring,
//              .ring_entries = number_of_buffers,
//              .bgid = 0
//      };
//
//      ret = io_uring_register_buf_ring(ring, &reg, 0);
//      if (ret) {
//            fprintf(stderr, "buf_ring init failed: %s\n"
//                            "NB This requires a kernel version >= 6.0\n",
//                    strerror(-ret));
//            return ret;
//      }
//
//      for (i = 0; i < BUFFERS; i++) {
//            io_uring_buf_ring_add(ctx->buf_ring, get_buffer(ctx, i), buffer_size(ctx), i,
//                                  io_uring_buf_ring_mask(BUFFERS), i);
//      }
//      io_uring_buf_ring_advance(ctx->buf_ring, BUFFERS);
//
//      return 0;
//}

void add_send(int socketfd, int buff_id){
      struct io_uring_sqe* sqe;
      struct request* req;

      req = malloc(sizeof(struct request));

      req->buffer_id = buff_id;
      req->type = EVENT_TYPE_SEND;

      sqe = io_uring_get_sqe(ring);
      if(sqe == NULL)
            printf("ERROR while getting the sqe\n");

      io_uring_prep_sendmsg(sqe,socketfd,&send_msgs[buff_id],0);
      io_uring_sqe_set_data(sqe, req);

      if(fixed_file)
            io_uring_sqe_set_flags(sqe,IOSQE_FIXED_FILE);
      if(async)
            io_uring_sqe_set_flags(sqe,IOSQE_ASYNC);
}

void add_recv_multishot(int socketfd) {
      struct msghdr *msghdr;
      struct sockaddr_in *src_add;
      struct iovec *iov;
      struct request *req;
      struct io_uring_sqe *sqe;

      req = malloc(sizeof(struct request));
      sqe = io_uring_get_sqe(ring);
      if (sqe == NULL)
            printf("ERROR while getting the sqe\n");


      printf("addin receive for socket %d\n", socketfd);
      req->msg = &msg;
      req->type = EVENT_TYPE_RECV;
      req->socket = socketfd;
      io_uring_prep_recvmsg_multishot(sqe,socketfd,&msg,MSG_TRUNC);
      io_uring_sqe_set_flags(sqe,IOSQE_BUFFER_SELECT);
      io_uring_sqe_set_flags(sqe,IOSQE_FIXED_FILE);
      io_uring_sqe_set_data(sqe,req);
      sqe->buf_group = grp_id;
      io_uring_submit(ring);
}

void handle_send(struct io_uring_cqe* cqe){
      struct request* req = (struct request*)io_uring_cqe_get_data(cqe);
      int mask = io_uring_buf_ring_mask(number_of_buffers);
      int buff_id = req->buffer_id;


      if(cqe->res < 0){
            printf("error on send,  number:%d\n",cqe->res);
      }

      io_uring_buf_ring_add(buff_ring,buffers[buff_id],2000,buff_id, mask,1);
      io_uring_buf_ring_advance(buff_ring,1);

      packets_sent++;
}

void handle_recv(struct io_uring_cqe* cqe){
      unsigned int buff_idx;
      struct io_uring_recvmsg_out* out_msg;
      struct request* req = (struct request*)io_uring_cqe_get_data(cqe);

      if(!start){
            start = 1;
            alarm(duration);
            printf("alarm set\n");
      }
      if(cqe->res < 0){
            printf("error on receive,  number:%d\n",cqe->res);
            exit(1);
      }

      if(!(cqe->flags & IORING_CQE_F_MORE)) {
            printf("Need to readd recv\n");
            add_recv_multishot(req->socket);
      }

      buff_idx = cqe->flags >> IORING_CQE_BUFFER_SHIFT;
      out_msg = io_uring_recvmsg_validate(&buffers[buff_idx],size,&msg);
      if(out_msg == NULL) {
            printf("Something wrong with recv buffers (not validated)\n");
            return;
      }

      send_iovecs[buff_idx] = (struct iovec) {
              .iov_base = io_uring_recvmsg_payload(out_msg, &msg),
              .iov_len = io_uring_recvmsg_payload_length(out_msg, cqe->res, &msg)
      };
      send_msgs[buff_idx] = (struct msghdr) {
              .msg_namelen = out_msg->namelen,
              .msg_name = io_uring_recvmsg_name(out_msg),
              .msg_control = NULL,
              .msg_controllen = 0,
              .msg_iov = &send_iovecs[buff_idx],
              .msg_iovlen = 1
      };

      add_send(req->socket, (int)buff_idx);
}

void start_loop(int socketfd){
      struct __kernel_timespec timespec;
      timespec.tv_sec = 0;
      timespec.tv_nsec = 100000000;

      buff_ring = init_buff_ring();
      add_recv_multishot(socketfd);

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


      fixed_files[0] = socketfd;
      if(io_uring_register_files(ring,fixed_files,1)<0){
            printf("Register file error\n");
            exit(-1);
      }
      socketfd = 0;

      memset(&msg, 0, sizeof(msg));
      msg.msg_namelen = sizeof(struct sockaddr_storage);
      msg.msg_controllen = 0;
      send_iovecs = malloc(sizeof(struct iovec)*number_of_buffers);
      send_msgs = malloc(sizeof(struct msghdr)*number_of_buffers);
      start_loop(socketfd);
}
