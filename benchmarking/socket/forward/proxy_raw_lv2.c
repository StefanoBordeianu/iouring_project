#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <linux/ip.h>
#include<net/ethernet.h>
#include <linux/if_packet.h>
#include <sys/ioctl.h>
#include <net/if.h>


long pkt = 0;
int duration = 10;
int start = 0;

void sig_handler(int signum){
      printf("\nReceived: %ld packets/s\n",pkt/duration);
      printf("Now closing\n\n");
      exit(0);
}

struct sockaddr_in handle_buffer(char* buffer,int size){
      struct sockaddr_in res;
      memset(&res,0,sizeof(res));

      struct iphdr* iphdr = (struct iphdr*) buffer;
      res.sin_addr.s_addr = iphdr->saddr;
      iphdr->saddr = iphdr->daddr;
      iphdr->daddr = res.sin_addr.s_addr;

      return res;
}


int main(int argc, char *argv[]){
      int sockfd;
      struct sockaddr_ll recv_add, send_adr;
      int port = 2020;
      int size = 64;
      struct sockaddr_ll bind_ll;
      struct ifreq if_idx;
      struct msghdr msghdr;
      struct iovec iov;
      int op = 1;
      char interface[] = "ens1f1np1";

      signal(SIGALRM,sig_handler);
      memset(&recv_add,0,sizeof(recv_add));
      memset(&send_adr,0,sizeof(send_adr));
      memset(&bind_ll,0,sizeof(bind_ll));

      if(argc>1)
            port = atoi(argv[1]);
      if(argc>2)
            duration = atoi(argv[2]);
      if(argc>3)
            size = atoi(argv[3]);


      if((sockfd = socket(AF_PACKET,SOCK_RAW,htons(ETH_P_IP)))<0){
            perror("socket");
            return 0;
      }
      memset(&if_idx, 0, sizeof(struct ifreq));
      strncpy(if_idx.ifr_name, interface ,9);

      if (ioctl(sockfd, SIOCGIFINDEX, &if_idx) < 0)
            perror("SIOCGIFINDEX");

      bind_ll.sll_family = AF_PACKET;
      bind_ll.sll_protocol = htons(ETH_P_IP);
      bind_ll.sll_ifindex = if_idx.ifr_ifindex;


//      if(bind(sockfd,(const struct sockaddr*)&listen_add,sizeof(listen_add)) < 0){
//            perror("bind");
//            return 0;
//      }

      char buffer[size];
      iov.iov_len = size;
      iov.iov_base = buffer;

      msghdr.msg_name = &recv_add;
      msghdr.msg_namelen = sizeof(struct sockaddr_ll);
      msghdr.msg_iov = &iov;
      msghdr.msg_iovlen = 1;

      while(1){
            long res;
            if((res = recvmsg(sockfd,&msghdr,0))<0){
                  printf("recv error:%ld\n",res);
            }

            recv_add.sll_ifindex = if_idx.ifr_ifindex;
            recv_add.sll_protocol = htons(ETH_P_IP);
            recv_add.sll_halen = ETH_ALEN;
            recv_add.sll_addr[0] = 0x9c;
            recv_add.sll_addr[1] = 0xdc;
            recv_add.sll_addr[2] = 0x71;
            recv_add.sll_addr[3] = 0x5d;
            recv_add.sll_addr[4] = 0x31;
            recv_add.sll_addr[5] = 0xd1;



            if((res = sendto(sockfd, msghdr.msg_iov->iov_base , size,0, (struct sockaddr*)&recv_add, sizeof(struct sockaddr_ll)))<0){
                  printf("send error:%ld\n",res);
            }
      }

}