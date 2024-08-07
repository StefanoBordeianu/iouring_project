#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>

long pkt = 0;
int duration = 10;
int start = 0;
long tot_send = 0;

void sig_handler(int signum){
      printf("\nReceived: %ld packets\n",pkt/duration);
      printf("\nSent: %ld bytes\n",tot_send);

      printf("Now closing\n\n");
      exit(0);
}

int main(int argc, char *argv[]){
      int sockfd;
      struct sockaddr_in listen_add, send_adr;
      int port = 2020;
      int size = 64;
      signal(SIGALRM,sig_handler);

      if(argc>1)
            port = atoi(argv[1]);
      if(argc>2)
            duration = atoi(argv[2]);
      if(argc>3) {
            size = atoi(argv[3]);
      }


      if((sockfd = socket(AF_INET, SOCK_DGRAM, 0))<0){
            perror("socket");
            return 0;
      }

      char interface[] = "ens1f1np1";
      if(setsockopt(sockfd, SOL_SOCKET, SO_BINDTODEVICE, interface, 13)<0){
            perror("Bind to device\n");
            return 0;
      }

      memset(&listen_add,0,sizeof(listen_add));
      memset(&send_adr,0,sizeof(send_adr));
      listen_add.sin_family = AF_INET;
      listen_add.sin_addr.s_addr = INADDR_ANY;
      listen_add.sin_port = htons(port);

      if(bind(sockfd,(const struct sockaddr*)&listen_add,sizeof(listen_add)) < 0){
            perror("bind");
            return 0;
      }

      char buffer[1500];
      socklen_t len = sizeof(send_adr);
      int n;
      struct iovec send_iovec[1];
      struct  msghdr send_msg;
      memset(&send_msg,0, sizeof(send_msg));

      while(1) {
            n = recvfrom(sockfd, buffer, 1500, 0, (struct sockaddr *) &send_adr, &len);
            if(n<0){
                  perror("recv\n");
                  return -1;
            }



            if (!start) {
                  start = 1;
                  alarm(duration);
            }

            send_iovec[0].iov_base = buffer;
            send_iovec[0].iov_len = n;
            send_msg.msg_name = &send_adr;
            send_msg.msg_namelen = sizeof(send_adr);
            send_msg.msg_iov = send_iovec;
            send_msg.msg_iovlen = 1;
            n = sendmsg(sockfd,&send_msg,0);

//            //try and set the IP manually
//            send_adr.sin_addr.s_addr = inet_addr("192.168.1.2");
//            send_adr.sin_family = AF_INET;
//            send_adr.sin_port = htons(port);
//
//             sendto(sockfd,buffer,size,0,(struct sockaddr*) &send_adr,(ssize_t )len);
            if(n<=0){
                  if(n==0)
                        printf("sended zero bytes\n");
                  else
                        perror("send\n");
                  return -1;
            }
            tot_send +=n;
            pkt++;
      }
}