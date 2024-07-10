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
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <sys/ioctl.h>
#include <net/if.h>


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
int compute_check = 0;

struct io_uring* ring;
int start = 0;
long packets_received = 0;
long packets_sent = 0;
long total_events = 0;
int fixed_files[10];
struct ifreq if_idx;
struct ifreq if_mac;

struct request{
    struct msghdr* msg;
    int type;
    int socket;
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

      while((opt =getopt(argc,argv,"hs:p:d:b:TACSDi:r:FPNn:c")) != -1) {
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
                  case 'c':
                        compute_check = 1;
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
      struct sockaddr_ll bind_ll;
      long res;
      char interface[] = "ens1f1np1";

      if((socketfd = socket(AF_PACKET,SOCK_RAW,htons(ETH_P_IP)))<0){
            perror("socket");
            return 0;
      }


      memset(&if_idx, 0, sizeof(struct ifreq));
      strncpy(if_idx.ifr_name, interface ,9);
      if (ioctl(socketfd, SIOCGIFINDEX, &if_idx) < 0)
            perror("SIOCGIFINDEX");
      memset(&if_mac, 0, sizeof(struct ifreq));

      strncpy(if_mac.ifr_name, interface, 9);
      if (ioctl(socketfd, SIOCGIFHWADDR, &if_mac) < 0)
            perror("SIOCGIFHWADDR");


      bind_ll.sll_family = AF_PACKET;
      bind_ll.sll_protocol = htons(ETH_P_IP);
      bind_ll.sll_ifindex = if_idx.ifr_ifindex;

      printf("INDEX OF INTERFACE %d\n",if_idx.ifr_ifindex);

      if((res = bind(socketfd, (struct sockaddr*)&bind_ll, sizeof(struct sockaddr_ll)))<0){
            printf("bind error:%ld\n",res);
      }

      return socketfd;
}

/* Compute checksum for count bytes starting at addr, using one's complement of one's complement sum*/
unsigned short compute_checksum(unsigned short *addr, unsigned int count) {
      register unsigned long sum = 0;
      while (count > 1) {
            sum += * addr++;
            count -= 2;
      }
      //if any bytes left, pad the bytes and add
      if(count > 0) {
            sum += ((*addr)&htons(0xFF00));
      }
      //Fold sum to 16 bits: add carrier to result
      while (sum>>16) {
            sum = (sum & 0xffff) + (sum >> 16);
      }
      //one's complement
      sum = ~sum;
      return ((unsigned short)sum);
}

void compute_ip_checksum(struct iphdr* iphdrp){
      iphdrp->check = 0;
      iphdrp->check = compute_checksum((unsigned short*)iphdrp, iphdrp->ihl<<2);
}

void handle_buffer(char* buffer, struct sockaddr_ll* addr){
      struct ether_header *eh = (struct ether_header *) buffer;
      struct iphdr *iph = (struct iphdr *) (buffer + sizeof(struct ether_header));
      struct udphdr *udph = (struct udphdr *) (buffer + sizeof(struct iphdr) + sizeof(struct ether_header));

      eh->ether_dhost[0] = eh->ether_shost[0];
      eh->ether_dhost[1] = eh->ether_shost[1];
      eh->ether_dhost[2] = eh->ether_shost[2];
      eh->ether_dhost[3] = eh->ether_shost[3];
      eh->ether_dhost[4] = eh->ether_shost[4];
      eh->ether_dhost[5] = eh->ether_shost[5];
      eh->ether_shost[0] = ((uint8_t *)&if_mac.ifr_hwaddr.sa_data)[0];
      eh->ether_shost[1] = ((uint8_t *)&if_mac.ifr_hwaddr.sa_data)[1];
      eh->ether_shost[2] = ((uint8_t *)&if_mac.ifr_hwaddr.sa_data)[2];
      eh->ether_shost[3] = ((uint8_t *)&if_mac.ifr_hwaddr.sa_data)[3];
      eh->ether_shost[4] = ((uint8_t *)&if_mac.ifr_hwaddr.sa_data)[4];
      eh->ether_shost[5] = ((uint8_t *)&if_mac.ifr_hwaddr.sa_data)[5];

      addr->sll_ifindex = if_idx.ifr_ifindex;
      addr->sll_protocol = htons(ETH_P_IP);
      addr->sll_halen = ETH_ALEN;
      addr->sll_addr[0] = eh->ether_dhost[0];
      addr->sll_addr[1] = eh->ether_dhost[1];
      addr->sll_addr[2] = eh->ether_dhost[2];
      addr->sll_addr[3] = eh->ether_dhost[3];
      addr->sll_addr[4] = eh->ether_dhost[4];
      addr->sll_addr[5] = eh->ether_dhost[5];

      if(compute_check) {
            iph->ttl = iph->ttl - 1;
            compute_ip_checksum(iph);
      }
}

void add_send(struct request* req, int lenght_to_send){
      struct msghdr* msghdr;
      struct io_uring_sqe* sqe;
      struct sockaddr_ll* addr;
      int socketfd;

      socketfd = req->socket;
      msghdr = req->msg;
      if(compute_check)
            handle_buffer(msghdr->msg_iov->iov_base,msghdr->msg_name);
      addr = (struct sockaddr_ll*)msghdr->msg_name;

      req->type = EVENT_TYPE_SEND;
      sqe = io_uring_get_sqe(ring);
      if(sqe == NULL)
            printf("ERROR while getting the sqe\n");

      addr->sll_ifindex = if_idx.ifr_ifindex;
      addr->sll_protocol = htons(ETH_P_IP);
      addr->sll_halen = ETH_ALEN;

      io_uring_prep_sendto(sqe,socketfd,msghdr->msg_iov->iov_base, lenght_to_send,0,(struct sockaddr*)addr,sizeof(struct sockaddr_ll));
      io_uring_sqe_set_data(sqe, req);
      if(fixed_file)
            io_uring_sqe_set_flags(sqe,IOSQE_FIXED_FILE);
      if(async)
            io_uring_sqe_set_flags(sqe,IOSQE_ASYNC);
}

void add_starting_receive(int socketfd){
      struct msghdr* msghdr;
      struct sockaddr_ll* addr;
      struct iovec* iov;
      struct request* req;
      struct io_uring_sqe* sqe;

      req = malloc(sizeof(struct request));
      iov = malloc(sizeof(struct iovec));
      msghdr = malloc(sizeof(struct msghdr));
      addr = malloc(sizeof(struct sockaddr_ll));
      memset(req,0,sizeof(*req));
      memset(iov,0,sizeof(*iov));
      memset(msghdr,0,sizeof(*msghdr));
      memset(addr,0,sizeof(*addr));

      sqe = io_uring_get_sqe(ring);
      if(sqe == NULL)
            printf("ERROR while getting the sqe\n");

      iov->iov_len = 1500;
      iov->iov_base = malloc(1500);
      memset(iov->iov_base,0,1500);
      
      msghdr->msg_name = addr;
      msghdr->msg_namelen = sizeof(struct sockaddr_ll);
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

      memset(req->msg->msg_iov->iov_base,0,1500);
      memset(req->msg->msg_name,0,sizeof(struct sockaddr_ll));
      req->msg->msg_iov->iov_len = 1500;

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
      }

      req->msg->msg_iov->iov_len = cqe->res;
      packets_sent++;
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
      add_send(req, cqe->res);
}

void start_loop(int socketfd){
      struct __kernel_timespec timespec;
      timespec.tv_sec = 0;
      timespec.tv_nsec = 100000000;

      for(int i=0;i<initial_count;i++){
            if(fixed_file==1)
                  add_starting_receive(0);
            else
                  add_starting_receive(socketfd);
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
