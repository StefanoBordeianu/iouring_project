#define UDP_PORT 6000
#include <iostream>
#include <event.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

using namespace std;

int g_count = 0;
struct timeval stTv;

//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
// TIMER FUNCTION
//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
void timer_function(int x, short int y, void *pargs)
{
      printf("nn**********Count is %dnn",g_count);
      g_count = 0 ;
      event_add((struct event*)pargs,&stTv);
}



//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
// Event Function
//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
void func_for_eve1(int x, short int y, void *pargs)
{
      unsigned int unFromAddrLen;
      int nByte = 0;
      char aReqBuffer[512];
      struct sockaddr_in stFromAddr;

      unFromAddrLen = sizeof(stFromAddr);

      if ((nByte = recvfrom(x, aReqBuffer, sizeof(aReqBuffer), 0,
                            (struct sockaddr *)&stFromAddr, &unFromAddrLen)) == -1)
      {
            printf("error occured while receivingn");
      }

//printf("Function called buffer is %sn",aReqBuffer);
      g_count++;

}

//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
// MAIN
//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
int main(int argc, char **argv)
{
      struct event_base *base ;
      struct event g_Timer;
      int val1,val2;
      if(argc == 3)
      {
            val1 = atoi(argv[1]);
            val2 = atoi(argv[2]);
            printf("Value is %d %dn",val1,val2);
      }
      else return 0;

      struct event g_eve[val2-val1];
      int udpsock_fd[val2-val1];
      struct sockaddr_in stAddr[val2-val1];

      base = event_init();

      for(int i=0; i< val2-val1 ; i++)
      {
            if ((udpsock_fd[i] = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
            {
                  printf("ERROR - unable to create socket:n");
                  exit(-1);
            }

//Start : Set flags in non-blocking mode
            int nReqFlags = fcntl(udpsock_fd[i], F_GETFL, 0);
            if (nReqFlags< 0)
            {
                  printf("ERROR - cannot set socket options");
            }

            if (fcntl(udpsock_fd[i], F_SETFL, nReqFlags | O_NONBLOCK) < 0)
            {
                  printf("ERROR - cannot set socket options");
            }
// End: Set flags in non-blocking mode
            memset(&stAddr[i], 0, sizeof(struct sockaddr_in));
//stAddr.sin_addr.s_addr = inet_addr("192.168.64.1555552");
            stAddr[i].sin_addr.s_addr = INADDR_ANY; //listening on local ip
            stAddr[i].sin_port = htons(UDP_PORT+i+val1);
            stAddr[i].sin_family = AF_INET;


            int nOptVal = 1;
            if (setsockopt(udpsock_fd[i], SOL_SOCKET, SO_REUSEADDR,
                           (const void *)&nOptVal, sizeof(nOptVal)))
            {
                  printf("ERROR - socketOptions: Error at Setsockopt");

            }

            if (bind(udpsock_fd[i], (struct sockaddr *)&stAddr[i], sizeof(stAddr[i])) != 0)
            {
                  printf("Error: Unable to bind the default IP n");
                  exit(-1);
            }

            event_set(&g_eve[i], udpsock_fd[i], EV_READ | EV_PERSIST, func_for_eve1, &g_eve[i]);
            event_add(&g_eve[i], NULL);
      }

/////////////TIMER START///////////////////////////////////
      stTv.tv_sec = 3;
      stTv.tv_usec = 0;
      event_set(&g_Timer, -1, EV_TIMEOUT , timer_function, &g_Timer);
      event_add(&g_Timer, &stTv);
////////////TIMER END/////////////////////////////////////

      event_base_dispatch(base);
      return 0;
}