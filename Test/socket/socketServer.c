#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>

#define PORT 2020

long i=0;
int socketfd,connected_socket_fd;

int init() {
      long bytes_sen, rec;
      struct sockaddr_in add;
      long len = sizeof(add);
      int opt = 1;
      char rec_buff[5000];
      char* buff = "Hello there\n";


      socketfd = socket(AF_INET, SOCK_STREAM, 0);
      if(socketfd < 0){
            printf("SERVER: Error while creating the socket\n");
            return -1;
      }
      if(setsockopt(socketfd,SOL_SOCKET,SO_REUSEADDR|SO_REUSEPORT,
                    &opt,sizeof (opt))){
            printf("SERVER: Socket options error\n");
            return -1;
      }

      add.sin_port = htons(PORT);
      add.sin_family = AF_INET;
      add.sin_addr.s_addr = INADDR_ANY;

      if (bind(socketfd, (struct sockaddr*)&add,
               sizeof(add))< 0){
            printf("SERVER: Error binding\n");
            return -1;
      }

      if(listen(socketfd,3)){
            printf("SERVER: Error listening\n");
            return -1;
      }

      if((connected_socket_fd = accept(socketfd,(struct sockaddr*)&add,
                                       (socklen_t *) &len) )< 0){
            printf("SERVER: Error while accepting the connection\n");
            return -1;
      }


      int start = 0;
      while(1){
            if(!start){
                  alarm(10);
                  start = 1;
            }
            rec = recv(connected_socket_fd, rec_buff, 1024, 0);
            i++;
            if (rec < 0) {
                  printf("SERVER: Error receiving\n");
                  return -1;
            }
      }
}

void sig_handler(int signum){
      printf("\nReceived: %ld packets\n",i);
      printf("SERVER: IM NOW CLOSING\n");
      close(connected_socket_fd);
      shutdown(socketfd,SHUT_RDWR);
}

int main(){
      printf("Hello, im the server :D!\n");

      int res;

      signal(SIGALRM,sig_handler);
      res = init();
      return res;
}
