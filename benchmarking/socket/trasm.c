#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <signal.h>

#define DEST_IP "10.10.1.2"  // Destination IP address
#define DEST_PORT 2020       // Destination UDP port
#define BUFFER_SIZE 1500      // Size of the buffer to send

long pkts = 0;
int sockfd;
int start = 0;
int duration = 10;

void sig_handler(int signum){
      printf("\ntrasmitted: %ld packets\n",pkts);
      close(sockfd);
      printf("Now closing\n\n");
      exit(0);
}


int main(int argc, char *argv[]){

      struct sockaddr_in dest_addr;
      char buffer[BUFFER_SIZE];
      if(argc>=2)
            duration = atoi(argv[1]);

      signal(SIGALRM,sig_handler);
      sockfd = socket(AF_INET, SOCK_DGRAM, 0);
      if (sockfd < 0) {
            perror("Socket creation failed");
            return 1;
      }


      memset(&dest_addr, 0, sizeof(dest_addr));
      dest_addr.sin_family = AF_INET;
      dest_addr.sin_port = htons(DEST_PORT);
      dest_addr.sin_addr.s_addr = inet_addr(DEST_IP);


      memset(buffer, 'A', sizeof(buffer));  // Filling the buffer with 1500 'A' characters

      while(1) {
            if(!start){
                  start = 1;
                  alarm();
            }
            ssize_t sent_bytes = sendto(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *) &dest_addr,
                                        sizeof(dest_addr));
            if (sent_bytes < 0) {
                  perror("Send failed");
                  close(sockfd);
                  return 1;
            }
            pkts++;
      }

      // Close the socket
      close(sockfd);

      return 0;
}
