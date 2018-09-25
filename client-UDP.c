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

    int i = 0;
    for (; i < strlen(ip_address); i++) {
        if ((ip_address[i] < '0' || ip_address[i] > '9') && ip_address[i] != '.') {
            printf("Error in IP address\n");
            return 1;
        }
    }


    int port;
    printf("Enter a port: ");
    scanf("%d", &port);
    printf("\n");
    getchar();

    if (port < 1024 || port > 65535) {
        printf("Error in port\n");
            return 1;
    }

    struct sockaddr_in serveraddr;
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(port);
    serveraddr.sin_addr.s_addr = inet_addr(ip_address); 

    uint len = sizeof(serveraddr);

    char file_name[PACKET_SIZE];
    char file_write_name[PACKET_SIZE];
    char buffer[PACKET_SIZE + HEADER_SIZE];

    char *newline;

    printf("Enter a file name to get:\n");
    if (fgets(file_name, sizeof(file_name), stdin) == NULL) {
        return -1; // Input error / EOF
    }
    newline = strchr(file_name, '\n');
    if (newline) {
        // if a newline is present, we change the last char to NULL
        *newline = '\0';
    }

    printf("Enter a file name to create:\n");
    if (fgets(file_write_name, sizeof(file_write_name), stdin) == NULL) {
        return -1; // Input error / EOF
    }
    newline = strchr(file_write_name, '\n');
    if (newline) {
        // if a newline is present, we change the last char to NULL
        *newline = '\0';
    }

    // create our output file
    FILE *file_out = fopen(file_write_name, "w");

    sendto(sockfd, file_name, strlen(file_name)+1, 0, (struct sockaddr*)&serveraddr, len);

    while (1) {
        if (recvfrom(sockfd, buffer, PACKET_SIZE + HEADER_SIZE, 0, (struct sockaddr*)&serveraddr, &len) < 0) {
            printf("Error while retrieving message.\n");
            break;
        } else {
            // send acknowledgement (need to do error checking before this)
            sendto(sockfd, buffer, HEADER_SIZE, 0, (struct sockaddr*)&serveraddr, len); // send the first byte of the buffer
            
            // write to a file
            fwrite(buffer+1, 1, sizeof(buffer)-1, file_out);

            if ((*buffer) == (char)('A'+(2*WINDOW_SIZE)+1)) {
                // the header indicates this is the last packet

                // we really can't just break out, since we need to do some final error checking
                // but we can do this temporarily
                break;
            }
        }
        memset(buffer, 0, PACKET_SIZE+HEADER_SIZE);
        // need to find a way to avoid doing this but it is needed, otherwise if
        // we send less than PACKET_SIZE bytes, the end of the buffer
        // (which is old data) is written to the file
    }
    fclose(file_out);
    close(sockfd);

}

