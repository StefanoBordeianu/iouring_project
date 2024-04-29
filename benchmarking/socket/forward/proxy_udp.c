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

void sig_handler(int signum){
      printf("\nReceived: %ld packets\n",pkt/duration);
      printf("Now closing\n\n");
      exit(0);
}

int main(int argc, char *argv[]){
      int sockfd;
      struct sockaddr_in listen_add, send_adr;
      char buffer[64];
      int port = 2020;
      int size = 64;

      if(argc>1){
            port = atoi(argv[1]);
      }
      if(argc>2){
            duration = atoi(argv[2]);
      }
      if(argc>3){
            size = atoi(argv[3]);
      }

      signal(SIGALRM,sig_handler);

      if((sockfd = socket(AF_INET, SOCK_DGRAM, 0))<0){
            perror("socket");
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

      struct iovec send_iovec[1];
      struct  msghdr send_msg;
      memset(&send_msg,0, sizeof(send_msg));

      while(1) {
            n = recvfrom(sockfd, buffer, size, 0, (struct sockaddr *) &send_adr, &len);
//            for (int i=0; i<64; i++) {
//                  printf("%02x ", buffer[i]);
//                  if ((i+1)%16 == 0) printf("\n");
//            }

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
            send_msg.msg_flags = 0;
            send_msg.msg_controllen = 0;
            send_msg.msg_control = NULL;

            if (sendmsg(sockfd, &send_msg, 0) < 0){
                  perror("send");
                  return 0;
            }
            pkt++;
      }
}