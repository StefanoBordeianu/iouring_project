#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <errno.h>

#include "liburing.h"

#define BUFF_SIZE args.buffer_size
#define NUMBER_OF_BUFFERS args.numb_of_buffers


struct io_uring ring;
long packetsReceived;
long bytes_rec;
char** buffers;
int* data_ids;

struct request{
    int type;
    char* message;
};

struct args{
    int port;
    int numb_of_buffers;
    int duration;
    int size;
    int debug;
    int buffer_size;
    int numb_of_groups;
};

struct args args;

int parseArgs(int argc, char* argv[]){
      int opt;
      args.port = 2020;
      args.duration = 10;
      args.numb_of_buffers = 10000;
      args.debug = 0;

      while((opt =getopt(argc,argv,"hs:p:d:n:b:vg:")) != -1) {
            switch (opt) {
                  case 'p':
                        args.port = atoi(optarg);
                        break;
                  case 'n':
                        args.numb_of_buffers =  atoi(optarg);
                        break;
                  case 'd':
                        args.duration = atoi(optarg);
                        break;
                  case 'v':
                        args.debug = 1;
                        break;
                  case 's':
                        args.size = atoi(optarg);
                        break;
                  case 'g':
                        args.numb_of_groups = atoi(optarg);
                        break;
            }
      }
      return 0;
}

void initialize_ids(){
      data_ids = malloc(sizeof(int)*args.numb_of_groups);

      for (int i=0; i<args.numb_of_groups; i++) {
            data_ids[i] = i;
      }
}

struct io_uring_buf_ring* initialize_buffers(int id){

      struct io_uring_buf_reg reg = {};
      struct io_uring_buf_ring* br;
      int i;
      printf("SIZE: %ld\nSCpagesize:  %ld\n",(NUMBER_OF_BUFFERS * sizeof(struct io_uring_buf)),sysconf(_SC_PAGESIZE));

      if (posix_memalign((void **) &br, sysconf(_SC_PAGESIZE),
                         NUMBER_OF_BUFFERS * sizeof(struct io_uring_buf))){
            //printf("PRIMO %s\n",strerror(errno));
            return NULL;
      }

      reg.ring_addr = (unsigned long) br;
      reg.ring_entries = NUMBER_OF_BUFFERS;
      reg.bgid = id;
      if ((i = io_uring_register_buf_ring(&ring, &reg, 0))){
            printf("register error:     %s\n",strerror(i*-1));
            return NULL;
      }

      buffers = malloc(sizeof(char*) * NUMBER_OF_BUFFERS);
      for(i=0;i<NUMBER_OF_BUFFERS;i++)
            buffers[i] = malloc(BUFF_SIZE);

      io_uring_buf_ring_init(br);
      for (i = 0; i < NUMBER_OF_BUFFERS; i++) {
            io_uring_buf_ring_add(br, buffers[i], BUFF_SIZE, i,
                                  io_uring_buf_ring_mask(NUMBER_OF_BUFFERS),
                                  i);
      }

      io_uring_buf_ring_advance(br, NUMBER_OF_BUFFERS);
      return br;
}

int openListeningSocket(int port){
      int socketfd;
      struct sockaddr_in add;

      socketfd = socket(AF_INET, SOCK_DGRAM, 0);
      if(socketfd < 0){
            printf("SERVER: Error while creating the socket\n");
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

int add_recv_request(int socket, int id){
      struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);
      if(sqe==NULL)
            printf("NO SQEs available");
      io_uring_prep_recv_multishot(sqe, socket, NULL,0,0);
      sqe->buf_group = id;
      io_uring_sqe_set_data(sqe,&data_ids[id]);
      io_uring_sqe_set_flags(sqe, IOSQE_BUFFER_SELECT);
      return 1;
}

void startServer(int socketfd){

}

void sig_handler(int signum){
      printf("\nReceived: %ld packets of size %d\n",packetsReceived, args.size);
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


      startServer(socketfd);

}

