#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>

#define MAX_EVENTS 10000

long pkt = 0;
int duration = 10;
int start = 0;
long tot_send = 0;
int starting_port = 2020;
int number_of_sockets = 1;
int* sockets;
int epoll_fd;
long* pkts_recv_per_socket;
long* pkts_sent_per_socket;
struct epoll_event events[MAX_EVENTS];
struct epoll_event* evs;
long processed_events = 0;
int sink = 0;
int report = 0;

void sig_handler(int signum){
      if(report)
            for(int i=0;i<number_of_sockets;i++){
                  //printf("SOCKET index %d\n",i);
                  //printf("Received: %ld packets\n",pkts_recv_per_socket[i]);
                  printf("Sent: %ld packets\n",pkts_sent_per_socket[i]);
                  long speed = pkts_recv_per_socket[i]/duration;
                  printf("Speed: %ld packets/second\n\n", speed);

            }

      printf("Processed: %ld events\n",processed_events);
      printf("Processed: %ld events/s\n",processed_events/duration);
      printf("Now closing\n\n");
      free(pkts_recv_per_socket);
      free(pkts_sent_per_socket);
      free(sockets);
      free(evs);
      exit(0);
}

int parse_arguments(int argc, char* argv[]){
      int opt;

      while((opt =getopt(argc,argv,"p:d:k:KR")) != -1) {
            switch (opt) {
                  case 'p':
                        starting_port = atoi(optarg);
                        break;
                  case 'd':
                        duration = atoi(optarg);
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

int get_index(int sock){

      for(int i=0;i<number_of_sockets;i++)
            if(sock==sockets[i])
                  return i;

      return -1;
}


int main(int argc, char *argv[]) {
      signal(SIGALRM, sig_handler);


      if (parse_arguments(argc, argv) < 0)
            return -1;

      sockets = malloc(sizeof(int)*number_of_sockets);
      pkts_recv_per_socket = malloc(sizeof(long) * number_of_sockets);
      pkts_sent_per_socket = malloc(sizeof(long) * number_of_sockets);
      evs = malloc(sizeof(struct epoll_event) * number_of_sockets);

      for (int i = 0; i < number_of_sockets; i++)
            sockets[i] = create_socket(starting_port + i);


      //create epoll instance
      epoll_fd = epoll_create1(0);
      if (epoll_fd == -1) {
            perror("epoll_create1");
            exit(EXIT_FAILURE);
      }

      for (int i = 0; i < number_of_sockets; i++){
            evs[i].events = EPOLLIN;
            evs[i].data.fd = sockets[i];
            if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sockets[i], &evs[i]) == -1) {
                  printf("socket %d\n",i);
                  perror("epoll_ctl: adding socket number\n");
                  exit(EXIT_FAILURE);
            }
      }

      char buffer[1500];
      int numb_evs_available;
      struct sockaddr_in s_addr;
      socklen_t len = sizeof(s_addr);
      struct iovec send_iovec[1];
      struct  msghdr send_msg;
      memset(&send_msg,0, sizeof(send_msg));
      send_msg.msg_name = &s_addr;
      send_msg.msg_namelen = sizeof(s_addr);
      send_msg.msg_iov = send_iovec;
      send_msg.msg_iovlen = 1;
      send_iovec[0].iov_base = buffer;
      while(1){
            numb_evs_available = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
            if (numb_evs_available == -1) {
                  perror("epoll_wait");
                  exit(EXIT_FAILURE);
            }

            for(int i=0;i<numb_evs_available;i++){
                  int res;
                  int sock = events[i].data.fd;
                  int index = get_index(sock);
                  if(index<0){
                        printf("Error: NOT VALID SOCKET\n");
                        exit(1);
                  }

                  res = recvfrom(sock,buffer,1500,0, (struct sockaddr*)&s_addr, &len);
                  if(res<0){
                        perror("recv\n");
                        exit(1);
                  }

                  if(!start){
                        start = 1;
                        alarm(duration);
                  }
                  pkts_recv_per_socket[i]++;

                  if(!sink) {
                        send_iovec[0].iov_len = res;
                        res = sendmsg(sock, &send_msg, 0);
                        if (res <= 0) {
                              if (res == 0)
                                    printf("sended zero bytes\n");
                              else
                                    perror("send\n");
                              return -1;
                        }
                  }
                  pkts_sent_per_socket[i]++;
                  processed_events++;
            }
      }





}