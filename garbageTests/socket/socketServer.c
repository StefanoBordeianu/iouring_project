#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>

#define PORT 2020

int init() {
      int socketfd, connected_socket_fd;
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

      for(int i=0;i<1000;i++) {
            rec = recv(connected_socket_fd, rec_buff, 1024, 0);
            if (rec < 0) {
                  printf("SERVER: Error receiving\n");
                  return -1;
            }
            printf("received %d\n",i);
      }

      printf("SERVER: IM NOW CLOSING\n");
      close(connected_socket_fd);
      shutdown(socketfd,SHUT_RDWR);

      return 1;
}

int main(){
      printf("Hello, im the server :D!\n");

      int res;

      res = init();
      return res;
}
