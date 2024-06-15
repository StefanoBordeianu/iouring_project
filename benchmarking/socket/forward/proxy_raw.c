#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <linux/ip.h>

long pkt = 0;
int duration = 10;
int start = 0;

void sig_handler(int signum){
      printf("\nReceived: %ld packets\n",pkt/duration);
      printf("Now closing\n\n");
      exit(0);
}

struct sockaddr_in handle_buffer(char* buffer,int size){
      struct sockaddr_in res;
      memset(&res,0,sizeof(res));

      struct iphdr* iphdr = (struct iphdr*) buffer;
      res.sin_addr.s_addr = iphdr->saddr;
      //iphdr->saddr = iphdr->daddr;
      //iphdr->daddr = res.sin_addr.s_addr;

      return res;
}


int main(int argc, char *argv[]){
      int sockfd;
      struct sockaddr_in listen_add, send_adr;
      char buffer[64];
      int port = 2020;
      int size = 64;
      int op = 1;
      char interface[] = "ens1f1np1";

      signal(SIGALRM,sig_handler);

      if(argc>1)
            port = atoi(argv[1]);
      if(argc>2)
            duration = atoi(argv[2]);
      if(argc>3)
            size = atoi(argv[3]);


      if((sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_UDP))<0){
            perror("socket");
            return 0;
      }

      if(setsockopt(sockfd, IPPROTO_IP, IP_HDRINCL, &op, sizeof(op))<0){
            perror("IP header option\n");
            return 0;
      }
      if(setsockopt(sockfd, SOL_SOCKET, SO_BINDTODEVICE, interface, 13)<0){
            perror("Bind to device\n");
            return 0;
      }


      memset(&listen_add,0,sizeof(listen_add));
      memset(&send_adr,0,sizeof(send_adr));
      listen_add.sin_family = AF_INET;
      listen_add.sin_addr.s_addr = inet_addr("192.168.1.1");;
      listen_add.sin_port = htons(port);

      if(bind(sockfd,(const struct sockaddr*)&listen_add,sizeof(listen_add)) < 0){
            perror("bind");
            return 0;
      }

      socklen_t len = sizeof(send_adr);
      int n;

      while(1) {
            n = recvfrom(sockfd, buffer, size, 0, NULL, NULL);
            if(n<0){
                  perror("recv\n");
                  return -1;
            }

            if (!start) {
                  start = 1;
                  alarm(duration);
            }

            send_adr = handle_buffer(buffer,size);

            n = sendto(sockfd,buffer,size,0, (struct sockaddr*) &send_adr,len);
            if(n<=0){
                  if(n==0)
                        printf("sended zero bytes\n");
                  else
                        perror("send\n");
                  return -1;
            }
            //printf("%d\n",n);
            pkt++;
      }
}