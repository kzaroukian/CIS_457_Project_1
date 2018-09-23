#include <sys/socket.h>
#include <netinet/in.h> 
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>


int WINDOW_SIZE = 5; // we can handle 5 packets at a time
int HEADER_SIZE = 1; // need 3 bytes for identifying packets
int PACKET_SIZE = 1024; // we can send a max of 1024 bytes (excluding headers)

int main(int argc, char** argv) {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if(sockfd < 0) {
        printf("There was an ERROR(1) creating the socket\n");
        return 1;
    }

    int portNum;

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
    serveraddr.sin_port = htons(port); 
    serveraddr.sin_addr.s_addr = INADDR_ANY;

    int b = bind(sockfd, (struct sockaddr*)&serveraddr, sizeof(serveraddr)); //associates address with socket 
    if(b < 0){
        printf("There was an ERROR(2) binding failed\n");
        return 2;
    }

    char *read_buffer = (char*) malloc((PACKET_SIZE + HEADER_SIZE) * sizeof(char));

    while(1){
        uint len = sizeof(clientaddr);
        char file_name[PACKET_SIZE];
        int n = recvfrom(sockfd, file_name, PACKET_SIZE, 0, (struct sockaddr*)&clientaddr, &len);

        if(n < 0)
            printf("Time out on recieve.\n"); 
        else {
            printf("Got from client: %s\n", file_name);
            FILE * in = fopen (file_name, "r");
            if (in == NULL) {
                char error_line[] = "Error: File not found";
                printf("%s\n", error_line);
                sendto(sockfd, (char)('A'-1)+error_line, strlen(error_line)+1, 0, (struct sockaddr*)&clientaddr, len);
                // use what is essentially a -1 byte at the beginning to denote an error
                continue;
            }
            fseek(in, 0L, SEEK_END);
            long file_length = ftell(in);
            fseek(in, 0, SEEK_SET);

            int pack_ID = 0;
            int current_packet = 0;


            while (1) {
                *(read_buffer) = (char)('A'+pack_ID); // add the identifier to the beginning of the line
                // this identifier will be a char, starting at A, and ending at A + WINDOW_SIZE*2
                // this will end up being, in the case of the window size being 5, A through J
                
                int diff = file_length - ftell(in);
                int actual_packet_size = PACKET_SIZE;
                if (diff == 0)
                    break; // we have reached the end of the file
                if (diff < PACKET_SIZE) {
                    fread(read_buffer+1, sizeof(char), diff, in);
                    // we have less than PACKET_SIZE bytes left in the file, so only
                    // read diff number of bytes of the file into the read_buffer.
                    // 
                    // this will seek to the end of the file, so let the client
                    // know this is the last packet by putting what we can consider
                    // an EOF byte at the end
                    *(read_buffer) = (char)('A'+(2*WINDOW_SIZE)+1);
                    actual_packet_size = diff+1;
                } else {
                    fread(read_buffer+1, sizeof(char), PACKET_SIZE, in);
                    // read PACKET_SIZE bytes of the file into the read_buffer

                }
                
                printf("%s -> %d, %d\n", "packet ID: ", pack_ID, actual_packet_size);

                pack_ID++;
                if (pack_ID >= 2 * WINDOW_SIZE)
                    pack_ID = 0;
                
                if (sendto(sockfd, read_buffer, actual_packet_size, 0, (struct sockaddr*)&clientaddr, len) < 0) {
                    printf("sendto failed\n");
                    break;
                    // need to do error handling here, in the future
                }
                
            }
            //free(read_buffer);
        }
    }
}
