#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <signal.h>
#include <unistd.h>

#include "liburing.h"

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
int buffers_per_ring = 1024;
int buffer_size = 2048;

struct io_uring* ring;
int start = 0;
long* pkts_recv_per_socket;
long* pkts_sent_per_socket;
long total_events = 0;
int* sockets;
struct msgs* local_msgs;
struct io_uring_buf_ring** buff_rings;
char* buffers;
struct msghdr recv_msg;

struct msgs {
    struct msghdr msg;
    struct iovec iov;
};

struct request{
    int index;
    int type;
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

void init_data_structures(){
      sockets = malloc(sizeof(int)*number_of_sockets);
      pkts_recv_per_socket = malloc(sizeof(long)*number_of_sockets);
      pkts_sent_per_socket = malloc(sizeof(long)*number_of_sockets);
      local_msgs = malloc(sizeof(struct msgs)*number_of_sockets*buffers_per_ring);
      buff_rings = malloc(sizeof(struct io_uring_buf_ring*)*number_of_sockets);
      buffers = malloc(buffer_size*buffers_per_ring*number_of_sockets);
      memset(&recv_msg,0,sizeof(struct msghdr));
      recv_msg.msg_namelen = sizeof(struct sockaddr_storage);
      recv_msg.msg_controllen = 0;
}


int parse_arguments(int argc, char* argv[]){
      int opt;

      while((opt =getopt(argc,argv,"hs:p:d:b:TACSDi:r:FPNn:k:KR")) != -1) {
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
                  case 'k':
                        number_of_sockets = atoi(optarg);
                        break;
                  case 'K':
                        sink = 1;
                        break;
                  case 'R':
                        report = 1;
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


char* get_buffer(int group, int index){
      int group_size = buffers_per_ring*buffer_size;
      int offset = index*buffer_size;
      char* res = &buffers[(group*group_size)+offset];
      return res;
}

void recycle_buffer(int group, int index){
      io_uring_buf_ring_add(buff_rings[group], get_buffer(group, index), buffer_size, index,
                            io_uring_buf_ring_mask(buffers_per_ring), 0);
      io_uring_buf_ring_advance(buff_rings[group], 1);
}


//int init_buffers(int sock_index){
//      int ret, i;
//      void *mapped;
//      size_t buf_ring_size;
//      struct io_uring_buf_reg reg = { .ring_addr = 0,
//              .ring_entries = buffers_per_ring,
//              .bgid = sock_index };
//
//
//      buf_ring_size = (sizeof(struct io_uring_buf) + buffer_size) * buffers_per_ring;
//      mapped = mmap(NULL, buf_ring_size, PROT_READ | PROT_WRITE,
//                    MAP_ANONYMOUS | MAP_PRIVATE, 0, 0);
//      if (mapped == MAP_FAILED) {
//            fprintf(stderr, "buf_ring mmap: %s\n", strerror(errno));
//            return -1;
//      }
//
//      buff_rings[sock_index] = (struct io_uring_buf_ring *)mapped;
//
//      io_uring_buf_ring_init(buff_rings[sock_index]);
//
//      reg = (struct io_uring_buf_reg) {
//              .ring_addr = (unsigned long)buff_rings[sock_index],
//              .ring_entries = buf_ring_size,
//              .bgid = sock_index
//      };
//
//      ret = io_uring_register_buf_ring(ring, &reg, 0);
//      if (ret) {
//            fprintf(stderr, "buf_ring init failed: %s\n",strerror(-ret));
//            return ret;
//      }
//
//      for (i = 0; i < buffers_per_ring; i++) {
//            io_uring_buf_ring_add(buff_rings[sock_index], get_buffer(sock_index, i), buffer_size, i,
//                                  io_uring_buf_ring_mask(buffers_per_ring), i);
//      }
//      io_uring_buf_ring_advance(buff_rings[sock_index], buffers_per_ring);
//
//      printf("ring buffer initiated for socket %d",sock_index);
//      return 1;
//}
int init_buffers(int sock_index){
      int ret, i;

      buff_rings[sock_index] = io_uring_setup_buf_ring(ring,buffers_per_ring,sock_index,0,&ret);
      if (ret) {
            fprintf(stderr, "buf_ring init failed: %d\n",-ret);
            return ret;
      }

      for (i = 0; i < buffers_per_ring; i++) {
            io_uring_buf_ring_add(buff_rings[sock_index], get_buffer(sock_index, i), buffer_size, i,
                                  io_uring_buf_ring_mask(buffers_per_ring), i);
      }
      io_uring_buf_ring_advance(buff_rings[sock_index], buffers_per_ring);

      printf("ring buffer initiated for socket %d",sock_index);
      return 1;
}

void add_send(struct request* req){
// TODO
}


void add_multishot_recvmsg(int sock_index) {
      struct io_uring_sqe *sqe;
      struct request *req;

      sqe = io_uring_get_sqe(ring);
      if (sqe == NULL)
            printf("ERROR while getting the sqe\n");
      printf("Adding recvmsg_multishot to socket %d\n",sock_index);

      req = malloc(sizeof(struct request));
      req->type = EVENT_TYPE_RECV;
      req->index = sock_index;

      io_uring_prep_recvmsg_multishot(sqe, sock_index, &recv_msg, MSG_TRUNC);
      sqe->flags |= IOSQE_FIXED_FILE;
      sqe->flags |= IOSQE_BUFFER_SELECT;
      sqe->buf_group = sock_index;
      io_uring_sqe_set_data(sqe,req);
}


void handle_send(struct io_uring_cqe* cqe){
      struct request* req = (struct request*)io_uring_cqe_get_data(cqe);

}


void handle_recv(struct io_uring_cqe* cqe){
      struct request* req = (struct request*)io_uring_cqe_get_data(cqe);
      struct io_uring_recvmsg_out *o;
      int sock_index = req->index;
      int index;
      int res = cqe->res;

      if (cqe->res == -ENOBUFS)
            return ;

      if (!(cqe->flags & IORING_CQE_F_BUFFER) || cqe->res < 0) {
            fprintf(stderr, "recv cqe bad res %d\n", cqe->res);
            if (cqe->res == -EFAULT || cqe->res == -EINVAL)
                  fprintf(stderr,
                          "NB: This requires a kernel version >= 6.0\n");
            exit(1);
      }

      if (!start) {
            start = 1;
            alarm(duration);
      }

      index = cqe->flags >>16;
      o = io_uring_recvmsg_validate(get_buffer(sock_index, index), res, &recv_msg);

      if (!o) {
            fprintf(stderr, "bad recvmsg\n");
            return ;
      }
      if (o->namelen > recv_msg.msg_namelen) {
            fprintf(stderr, "truncated name\n");
            recycle_buffer(sock_index, index);
            return;
      }


}


void start_loop(){
      struct __kernel_timespec timespec;
      timespec.tv_sec = 0;
      timespec.tv_nsec = 100000000;

      for(int i=0;i<number_of_sockets;i++){
            add_multishot_recvmsg(i);
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
      struct io_uring_params params = {};
      ring = malloc(sizeof(struct io_uring));

      if(parse_arguments(argc,argv)<0)
            return -1;

      signal(SIGALRM,sig_handler);

      init_data_structures();

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

      for(int i=0;i<number_of_sockets;i++){
            sockets[i] = create_socket(starting_port+i);
            init_buffers(i);
      }

      if(io_uring_register_files(ring,sockets,number_of_sockets)<0){
            printf("Register file error\n");
            exit(-1);
      }

      start_loop();
}
