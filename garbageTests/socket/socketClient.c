#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#define PORT 8080

int init(){
    int socketfd;
    long bytes_sent, rec;
    struct sockaddr_in add;
    char* buff = "Hello Server! Im the client!\n";
    char rec_buff[50];

    socketfd = socket(AF_INET, SOCK_STREAM, 0);
    if(socketfd < 0){
        printf("CLIENT: Socket creation error\n");
        return -1;
    }

    add.sin_port = htons(PORT);
    add.sin_family = AF_INET;
    if(inet_pton(AF_INET,"127.0.0.1", &add.sin_addr) <= 0){
        printf("CLIENT: Error setting the address\n");
        return -1;
    }

    if(connect(socketfd, (struct sockaddr *)&add, sizeof(add)) < 0){
        printf("CLIENT: Error while connecting\n");
        return -1;
    }

    bytes_sent = send(socketfd, buff, strlen(buff), 0);
    if(bytes_sent < 0){
        printf("CLIENT: Error while sending\n");
        return -1;
    }
    printf("CLIENT: Message sent, number of bytes sent: %ld\n",bytes_sent);
    rec = recv(socketfd, rec_buff, 50, 0);
    if(rec < 0){
        int error = errno;
        printf("CLIENT: Error while receiving, ERROR=%d\n",error);
        return -1;
    }
    printf("CLIENT: string received: %s\n",rec_buff);

    close(socketfd);

    return 1;
}


int main(){
    printf("Hello, im the sender :D!\n");
    int res;

    res = init();
    return res;

}


