#include <sys/socket.h>
#include <netinet/in.h> 
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>


int main(int argc, char** argv) {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0); //notice second parameter is different thatn TCP
    if(sockfd < 0) {
        printf("There was an ERROR(1) creating the socket\n");
        return 1;
    }

    int portNum;
    char IPadr[20];

    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;

    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    int port;
    printf("Enter a port: ");
    scanf("%d", &port);
    printf("\n");
    getchar();

    struct sockaddr_in serveraddr, clientaddr;
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(port); //htons(portNum)** port num may be the same as the TCP stream, but  
    serveraddr.sin_addr.s_addr = INADDR_ANY; //inet_addr(IPadr)**

    int b = bind(sockfd, (struct sockaddr*)&serveraddr, sizeof(serveraddr)); //associates address with socket 
    if(b < 0){
        printf("There was an ERROR(2) binding failed\n");
        return 2;
    }

    while(1){
        uint len = sizeof(clientaddr);
        char file_name[1024];
        int n = recvfrom(sockfd, file_name, 1024, 0, (struct sockaddr*)&clientaddr, &len); // check last param //we use recvfrom in UDP
        // Anything else here? **
        if(n < 0)
            printf("Time out on recieve.\n"); 
        else {
            printf("Got from client: %s\n", file_name);

            // read file


            if (sendto(sockfd, line, strlen(line), 0, (struct sockaddr*)&clientaddr, len) < 0) {
                printf("sendto failed\n");
            }
        }
    }
}