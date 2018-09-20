#include <sys/socket.h>
#include <netinet/in.h> 
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char** argv) {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0); //notice second parameter is different thatn TCP
    if(sockfd < 0) {
        printf("There was an ERROR(1) creating the socket\n");
        return 1;
    }

    char ip_address[20];
    printf("Enter an IP address: ");
    scanf("%s", ip_address);
    printf("\n");

    int port;
    printf("Enter a port: ");
    scanf("%d", &port);
    printf("\n");
    getchar();

    struct sockaddr_in serveraddr;
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(port);
    serveraddr.sin_addr.s_addr = inet_addr(ip_address); 

    uint len = sizeof(serveraddr);

    char file_name[200];
    char buffer[1024];
    char *newline;

    printf("Enter a file name:\n");
    if (fgets(file_name, sizeof(file_name), stdin) == NULL) {
        return -1; // Input error / EOF
    }
    newline = strchr(file_name, '\n');
    if (newline) {
        // if a newline is present, we change the last char to NULL
        *newline = '\0';
    }

    sendto(sockfd, file_name, strlen(file_name)+1, 0, (struct sockaddr*)&serveraddr, len); //we use sendto in UDP
    if (recvfrom(sockfd, buffer, 1024, 0, (struct sockaddr*)&serveraddr, &len) < 0) {
        printf("Error while retrieving message.\n");
    } else {
        printf("Received from server: %s\n", buffer);
    }
    close(sockfd);

}

