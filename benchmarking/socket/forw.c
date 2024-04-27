#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

#define PORT 2020
#define IP_ADDR "192.168.1.1"
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
      struct sockaddr_in listen_add, send_add;
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
      memset(&send_add,0,sizeof(send_add));

      listen_add.sin_family = AF_INET;
      listen_add.sin_addr.s_addr = inet_addr("192.168.1.1");
      listen_add.sin_port = htons(PORT);

      if(bind(sockfd,(struct sockaddr*)&listen_add,sizeof(struct sockaddr)) < 0){
            fprintf (stderr, "errno = %d ", errno);
            perror("bind");
            return 0;
      }

      socklen_t len = sizeof(send_add);
      int n;

      struct iovec send_iovec[1];
      struct  msghdr send_msg;
      memset(&send_msg,0, sizeof(send_msg));
      send_add.sin_family = AF_INET;
      send_add.sin_port = htons(2020);
      send_add.sin_addr.s_addr = inet_addr("192.168.1.2");

      while(1){

            n = recvfrom(sockfd,buffer,sizeof(buffer),0,NULL,NULL);
            if(n<0){
                  fprintf (stderr, "errno = %d ", errno);
                  perror("recv");
                  return 0;
            }


            if(!start){
                  start = 1;
                  alarm(duration);
            }

            sendto(sockfd,buffer,64,0,(struct sockaddr*)&send_add,len);
            pkt++;
      }
}