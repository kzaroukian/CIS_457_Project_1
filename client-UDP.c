#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <math.h>

// gets checksum on the client side
size_t checksumCalculated(char *buffer, size_t len) {
  size_t i;
  size_t sum = 0;
  // checksum from server
  int val = (int)(buffer[2] * buffer[3]);
  printf("VAL %d\n", val);
  //sum += buffer[2]
  for(i = 0; i < len; i++) {
    printf("sum vals for adding: %c\n", buffer[i]);
    // may be better to write as *(buffer + i)

    if (i == 2) {
      // do nothing
    } else if (i == 3) {
      sum += val;
    } else {
      sum += (unsigned int) buffer[i];
    }
    // decides when to wrap
    if (sum & 0xFFFF0000) {
      sum &= 0xFFFF;
      sum++;
    }
  }
  //uint16_t finalSum = (uint16_t) sum;
  // gets 1s compliment and makes sure checksum is 16 bytes
  return ~(sum & 0xFFFF);
}

int main(int argc, char** argv) {
    int WINDOW_SIZE = 5; // we can handle 5 packets at a time
    int HEADER_SIZE = 3; // need 1 byte for identifying packets and 2 bytes for checksum
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
    char file_write_name[PACKET_SIZE];
    char buffer[PACKET_SIZE + HEADER_SIZE];

    char *newline;

    printf("Enter a file name to get:\n");
    if (fgets(file_name, sizeof(file_name), stdin) == NULL) {
        printf("Input error\n");
        return -1; // Input error / EOF
    }
    newline = strchr(file_name, '\n');
    if (newline) {
        // if a newline is present, we change the last char to NULL
        *newline = '\0';
    }

    printf("Enter a file name to create:\n");
    if (fgets(file_write_name, sizeof(file_write_name), stdin) == NULL) {
        printf("Input error\n");
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

    char lastAck = 'A'-1;
    char stored_packet_data[PACKET_SIZE * 2 * WINDOW_SIZE];

    while (1) {
        if (recvfrom(sockfd, buffer, PACKET_SIZE + HEADER_SIZE, 0, (struct sockaddr*)&serveraddr, &len) < 0) {
            printf("Error while retrieving packet\n");
            //break;
        } else {
            printf("header %c\n",*buffer);
            printf(" checksum %c\n", *(buffer + 1));
            // printf("%s\n", buffer);
            printf("last ack: %c\n", lastAck);
            printf("\n");
            if (*buffer == lastAck) {
                printf("if \n");
                //printf("if one\n");
                // we have received the same packet twice in a row
                sendto(sockfd, buffer, HEADER_SIZE, 0, (struct sockaddr*)&serveraddr, len);

            } else if (*buffer == lastAck+1 || ((*buffer == 'A') && (lastAck == 'A'+(2*WINDOW_SIZE)-1))) {
                // we have received the next packet

                printf("if 2\n");

                lastAck = *buffer; // set our last acknowledgement to this packet
                //  get checksum
                //int checksum = 0;
                int checksum = checksumCalculated(buffer, sizeof(buffer) - 1);
                printf("%d\n", checksum);
                if (checksum <= 0) {
                  // send acknowledgement (need to do error checking before this)
                  // sendto(sockfd, buffer, HEADER_SIZE, 0, (struct sockaddr*)&serveraddr, len); // send the first byte of the buffer
                } // else packet is corrupted
                sendto(sockfd, buffer, HEADER_SIZE, 0, (struct sockaddr*)&serveraddr, len); // send the first byte of the buffer

            } else if (*buffer > lastAck+1 || ((*buffer > lastAck-(2*WINDOW_SIZE)) && (*buffer < lastAck))) {
                printf("if 3\n");
                int checksum = checksumCalculated(buffer, sizeof(*buffer) - 1);
                printf("%d\n", checksum);

                // we have received a packet that we cannot ack yet
                memcpy((stored_packet_data+((*buffer-'A') * PACKET_SIZE)), buffer+1, strlen(buffer)-1);
                // this should copy the data from the buffer to the 'slot' of storage in the
                // stored_packet_data string. Packet 'A' will be stored from 0->1024, 'B'
                // from 1025->2049, packet 'Z' at ('Z' - 'A') * 1024

            } else {
                // we have received either a past packet or random data
            }


            // write to a file
            // also don't want to write check sum
            fwrite(buffer+1, 1, sizeof(buffer)-2, file_out);

            if ((*buffer) == (char)('A'+(2*WINDOW_SIZE)+1)) {
                printf("Received all packets\n");
                // the header indicates this is the last packet
                //
                // we really can't just break out, since we need to do some final error checking
                // but we can do this temporarily
                break;
            }
        }
        memset(buffer, 0, PACKET_SIZE+HEADER_SIZE);
        // need to find a way to avoid doing this but it is needed, otherwise if
        // we receive less than PACKET_SIZE bytes, the end of the buffer
        // (which is old data) is written to the file
    }

    close(sockfd);

}
