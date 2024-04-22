#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>

#define PORT 2020
#define IP_ADDR "192.168.1.2"
long pkt = 0;
int duration = 10;
int start = 0;

void sig_handler(int signum){
      printf("\nReceived: %ld packets\n",pkt/duration);
      printf("Now closing\n\n");
      exit(0);
}

int main(int argc, char *argv[]){
      int sockfd;
      struct sockaddr_in listen_add, send_adr;
      char buffer[64];

      if(argc>1){
            duration = atoi(argv[1]);
      }

      signal(SIGALRM,sig_handler);
      if((sockfd = socket(AF_INET, SOCK_DGRAM, 0))<0){
            perror("socket");
            return 0;
      }

      memset(&listen_add,0,sizeof(listen_add));
      memset(&send_adr,0,sizeof(send_adr));

      listen_add.sin_family = AF_INET;
      listen_add.sin_addr.s_addr =  inet_addr(IP_ADDR ) ;
      listen_add.sin_port = htons(PORT);

      if(bind(sockfd,(const struct sockaddr*)&listen_add,sizeof(listen_add)) < 0){
            perror("bind");
            return 0;
      }

      socklen_t len = sizeof(send_adr);
      int n;

      struct iovec send_iovec[1];
      struct  msghdr send_msg;
      memset(&send_msg,0, sizeof(send_msg));

      while(1){

            if(!start){
                  start = 1;
                  alarm(duration);
            }

            n = recvfrom(sockfd,buffer,sizeof(buffer),0,(struct sockaddr*)&send_adr,&len);
            send_iovec[0].iov_base = buffer;
            send_iovec[0].iov_len = n;
            send_msg.msg_name = &send_adr;
            send_msg.msg_namelen = sizeof(send_adr);
            send_msg.msg_iov = send_iovec;
            send_msg.msg_iovlen = 1;
            send_msg.msg_flags = 0 ;
            send_msg.msg_controllen = 0 ;
            send_msg.msg_control = NULL;
            sendmsg(sockfd , &send_msg , 0 );
            pkt++;
      }
}