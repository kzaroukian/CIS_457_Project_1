#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>


int WINDOW_SIZE = 5; // we can handle 5 packets at a time
int HEADER_SIZE = 3; // need 1 byte for identifying packets and 2 bytes for checksum
int PACKET_SIZE = 1024; // we can send a max of 1024 bytes (excluding headers)
int sendNextPacket(char* read_buffer, FILE* file_ptr, int *pack_ID, long file_length, int sockfd, struct sockaddr_in clientaddr, uint len);
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
        char client_response[PACKET_SIZE];
        int n = recvfrom(sockfd, client_response, PACKET_SIZE, 0, (struct sockaddr*)&clientaddr, &len);

        if(n < 0)
            printf("Time out on recieve.\n");
        else {
            printf("Got from client: %s\n", client_response);
            FILE * in = fopen (client_response, "r");
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
            char acks[WINDOW_SIZE*2];
            int current_ack_needed = 0;


            for (; current_packet < 5; current_packet++) {
                sendNextPacket(read_buffer, in, &pack_ID, file_length, sockfd, clientaddr, len);
            }
            while (1) {
                if (acks[current_ack_needed] == '1') {
                    // already have the ack, so send the next packet and continue
                    acks[current_ack_needed] = '0';
                    sendNextPacket(read_buffer, in, &pack_ID, file_length, sockfd, clientaddr, len);
                    current_ack_needed++;
                    continue;
                }
                if (recvfrom(sockfd, client_response, PACKET_SIZE, 0, (struct sockaddr*)&clientaddr, &len) < 0) {
                    printf("Did not receive data from the client\n");
                    break;
                    // TODO: change the timeout to a small value, if the timeout occurs there has been an error
                    // and we need to re-send the packet
                }
                printf("current_ack_needed: %d\n", current_ack_needed);
                if (client_response[0] == 'A' + current_ack_needed) {
                    // this is the packet that we need, so send the next packet
                    sendNextPacket(read_buffer, in, &pack_ID, file_length, sockfd, clientaddr, len);
                    acks[current_ack_needed] = '0';
                    current_ack_needed++;
                    if (current_ack_needed >= sizeof(acks)) {
                        current_ack_needed = 0;
                    }
                } else {
                    if (client_response[0] >= 'A' && client_response[0] <= 'A' + (2*WINDOW_SIZE)) {
                        // we have not yet received the packet we need, so store this acknowledgement for later
                        acks[client_response[0]-'A'] = '1';
                    } else {
                        // received a byte that is not in our window range
                    }
                }
            }
        }
    }
}

int checksumCalculated(char *buffer, size_t len) {
  size_t i;
  size_t sum;
  for(i = 0; i < len; i++) {
    //sum += (int) buffer[i];
    sum += (unsigned int) buffer[i];
    // decides when to wrap
    if (sum & 0xFFFF0000) {
      sum &= 0xFFFF;
      sum++;
    }
  }
  // makes sure checksum is only 16 bytes
  //uint16_t finalSum = (uint16_t) sum;
  printf("Checksum prior to: %zu\n", sum);
  // gets 1s compliment
  return ~(sum & 0xFFFF);
}

int sendNextPacket(char* read_buffer, FILE* file_ptr, int *pack_ID, long file_length, int sockfd, struct sockaddr_in clientaddr, uint len) {
    *(read_buffer) = (char)('A'+(*pack_ID)); // add the identifier to the beginning of the packet
    // this identifier will be a char, starting at A, and ending at A + WINDOW_SIZE*2
    // this will end up being, in the case of the window size being 5, A through J

    printf("Checksum: %d\n",checksumCalculated(read_buffer, len)/2);
    // add checksum to the packet
    // fits checksum into 2 bytes
    // puts first 8 bytes of checksum in 2nd bit of packet
    int answer = checksumCalculated(read_buffer, len) * -1;
    printf("Checksum: %d\n",answer);
    int checksumPartition = sqrt(answer);
    *(read_buffer + 1) = (char)(checksumPartition);
    printf("Checksum 8 bytes: %d\n",checksumPartition);
    printf("Char: %c\n",(char)checksumPartition);

    // puts second 8 bytes of checksum in 3rd bit of packet
    if (checksumPartition %2 != 0) {
      *(read_buffer + 2) = (char)(checksumPartition + 1);
    } else {
      *(read_buffer + 2) = (char)(checksumPartition);
    }
    printf("%s\n", read_buffer);

    int diff = file_length - ftell(file_ptr); // amount of data we have left
    int actual_packet_size = PACKET_SIZE;
    if (diff == 0)
        return 1; // we have reached the end of the file
    if (diff <= PACKET_SIZE) {
        fread(read_buffer+HEADER_SIZE, 1, diff, file_ptr);
        // we have less than PACKET_SIZE bytes left in the file, so only
        // read 'diff' number of bytes of the file into the read_buffer.
        //
        // this will seek to the end of the file, so let the client
        // know this is the last packet by putting what we can consider
        // an EOF byte at the beginning
        *(read_buffer) = (char)('A'+(2*WINDOW_SIZE)+1);
        actual_packet_size = diff+1;
    } else {
        fread(read_buffer+HEADER_SIZE, sizeof(char), PACKET_SIZE, file_ptr);
        // read PACKET_SIZE bytes of the file into the read_buffer

    }

    printf("%s -> %d, %d\n", "packet ID: ", *pack_ID, actual_packet_size);

    *pack_ID = (*pack_ID) + 1;
    if (*pack_ID >= 2 * WINDOW_SIZE)
        *pack_ID = 0;

    if (sendto(sockfd, read_buffer, actual_packet_size + HEADER_SIZE, 0, (struct sockaddr*)&clientaddr, len) < 0) {
        printf("sendto failed\n");
        return 2;
        // need to do error handling here, in the future
    }
    return 0;
}
