#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>

#define PORT 8080

int socketfd;
long packetsReceived = 0;
int duration = 5;
int port = 8080;
long bytes_rec;
int size;

int init() {

      struct sockaddr_in add;

      socketfd = socket(AF_INET, SOCK_DGRAM, 0);
      if(socketfd < 0){
            printf("SERVER: Error while creating the socket\n");
            return -1;
      }

      add.sin_port = htons(port);
      add.sin_family = AF_INET;
      add.sin_addr.s_addr = INADDR_ANY;

      if (bind(socketfd, (struct sockaddr*)&add,
               sizeof(add))< 0){
            printf("SERVER: Error binding\n");
            return -1;
      }
      return 1;
}

void serverLoop(){
      struct sockaddr_in addr;
      socklen_t len = sizeof(addr);
      int n;
      int start = 0;
      char buffer [size];

      while(1){
            n = recvfrom(socketfd, (char *)buffer, size,
                         MSG_WAITALL, ( struct sockaddr *) &addr,
                         &len);
            if(!start){
                  start = 1;
                  alarm(duration);
            }
            bytes_rec += n;
            packetsReceived++;
      }

}

void sig_handler(int signum){
      printf("\nReceived: %ld packets\n",packetsReceived);
      long speed = packetsReceived/duration;
      printf("Speed: %ld packets/second\n", speed);
      FILE* file;
      file = fopen("socketServerResults.txt","a");
      fprintf(file, "%ld\n", speed);
      fprintf(file,"%f\n", ((double)(bytes_rec*8))/(duration * 1000000));
      fclose(file);
      printf("Now closing\n\n");
      exit(0);
}

int main(int argc, char *argv[]){

      signal(SIGALRM,sig_handler);

      bytes_rec = 0;
      packetsReceived = 0;
      if(argc >= 2)
            port= atoi(argv[1]);

      if(argc >=3)
            duration = atoi(argv[2]);

      if(argc >=3)
            size = atoi(argv[3]);

      init();
      serverLoop();
      return 1;
}
