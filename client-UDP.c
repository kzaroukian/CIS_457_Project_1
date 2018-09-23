#include <sys/socket.h>
#include <netinet/in.h> 
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char** argv) {
    int WINDOW_SIZE = 5; // we can handle 5 packets at a time
    int HEADER_SIZE = 1; // need 1 byte for identifying packets
    int PACKET_SIZE = 1024; // we can send a max of 1024 bytes (excluding headers)
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
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

    char file_name[PACKET_SIZE];
    char buffer[PACKET_SIZE + HEADER_SIZE];
    //char result[100000]; // 100,000 bytes (100 kb) for testing purposes
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

    sendto(sockfd, file_name, strlen(file_name)+1, 0, (struct sockaddr*)&serveraddr, len);
    while (1) {
        if (recvfrom(sockfd, buffer, PACKET_SIZE + HEADER_SIZE, 0, (struct sockaddr*)&serveraddr, &len) < 0) {
            printf("Error while retrieving message.\n");
            break;
        } else {
            printf("****************************\n%s\n", buffer);
            // send acknowledgement (need to do error checking before this)
            //sendto(sockfd, buffer, HEADER_SIZE, 0, (struct sockaddr*)&serveraddr, len); // send the first 3 bytes of the buffer
            if ((*buffer) == (char)('A'+(2*WINDOW_SIZE)+1)) {
                // the header indicates this is the last packet
                break;
            }
        }
        memset(buffer, 0, PACKET_SIZE+HEADER_SIZE);
    }
    
    close(sockfd);

}

