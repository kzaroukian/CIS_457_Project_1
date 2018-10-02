#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <math.h>

uint16_t checksumCalculated(char* buffer, size_t len) {
  uint16_t i;
  uint16_t sum = 0;

  // gets approx checksum from server
  uint16_t val = (buffer[1] << 8) | (buffer[2] & 0xFF);
  printf("Val: %d\n", val);
  printf("Val Char: %c\n", (char)val);

  for(i = 1; i < len; i++) {
    if (i == 1) {
      // do nothing
    } else if (i == 2) {
      // adds checksu,
      sum += val;
    } else {
      sum += (unsigned int) buffer[i];
    }
    // decides when to wrap
    // makes sure sum is always 16 bits
    if (sum & 0xFFFF0000) {
      sum &= 0xFFFF;
      sum++;
    }
  }
  return ~(sum & 0xFFFF);
}

int main(int argc, char** argv) {
    int WINDOW_SIZE = 5; // we can handle 5 packets at a time
    int HEADER_SIZE = 3; // need 1 byte for identifying packets and 2 bytes for checksum
    int PACKET_SIZE = 1024; // we can send a max of 1024 bytes (excluding headers)
    char END_OF_FILE[4] = "EOF\0";
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if(sockfd < 0) {
        printf("There was an ERROR(1) creating the socket\n");
        return 1;
    }

    struct timeval timeout;
    timeout.tv_sec = 3;
    timeout.tv_usec = 0;

    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    /*
    char ip_address[20];
    printf("Enter an IP address: ");
    scanf("%s", ip_address);
    printf("\n");

    int port;
    printf("Enter a port: ");
    scanf("%d", &port);
    printf("\n");
    getchar();
    */
    int port = 9999;
    char ip_address[20] = "127.0.0.1";


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
    char file_name_received[PACKET_SIZE];
    while (1) {
        if (recvfrom(sockfd, file_name_received, PACKET_SIZE, 0, (struct sockaddr*)&serveraddr, &len) < 0) {
            printf("Filename packet was lost, resending...\n");
            sendto(sockfd, file_name, strlen(file_name)+1, 0, (struct sockaddr*)&serveraddr, len);
        } else {
            break;
        }
    }

    timeout.tv_sec = 0;
    timeout.tv_usec = 100000;

    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    char last_ack = 'A'-1;
    int stored_packet_data_len = PACKET_SIZE * 2 * WINDOW_SIZE;

    char *stored_packet_data = (char*) malloc(stored_packet_data_len * sizeof(char));

    memset(stored_packet_data, 0, PACKET_SIZE * 2 * WINDOW_SIZE);
    int packet_length;
    while (1) {
        // before we receive any data, check if we already have the next packet we need
        // stored into memory. If so, lets write it to the file and send the ack
        int packet_index = PACKET_SIZE * ((last_ack-'A')+1);
        if (*(stored_packet_data + packet_index) != 0) {
            while (1) {
                if (*(stored_packet_data + packet_index) != 0) {
                    printf("Writing stored packet data: %d at %d\n", (last_ack-'A')+1, packet_index);
                    // write to the file
                    int size = strlen(stored_packet_data + packet_index);
                    // if we have 2 concurrent packets, strlen will return the length of both
                    // packet data, so lets cap it at a max of PACKET_SIZE bytes
                    if (size > PACKET_SIZE) {
                        size = PACKET_SIZE;
                    }
                    last_ack ++;
                    if (last_ack >= 'A' + (2*WINDOW_SIZE)-1) {
                        last_ack = 'A';
                    }
                    fwrite(stored_packet_data + packet_index, 1, size, file_out);

                    memset(stored_packet_data + packet_index, 0, PACKET_SIZE);
                    sendto(sockfd, &last_ack, HEADER_SIZE, 0, (struct sockaddr*)&serveraddr, len);

                    packet_index = (packet_index + PACKET_SIZE) % stored_packet_data_len;
                    // we use modulo here in the case where we have 9 and 0 stored,
                    // so when we add PACKET_SIZE it will loop back to the beginning rather
                    // than give an error or skip the saved packet
                } else {
                    break;
                }
            }
            continue;
        }

        packet_length = recvfrom(sockfd, buffer, PACKET_SIZE + HEADER_SIZE, 0, (struct sockaddr*)&serveraddr, &len);
        if (packet_length < 0) {
            printf("Error while retrieving packet\n");
            continue;
        } else {
            int window_min = (last_ack - 'A' + 1) % (2*WINDOW_SIZE);
            int window_max = (window_min+WINDOW_SIZE-1) % (2*WINDOW_SIZE);

            printf("Min: %d, Max: %d\n", window_min, window_max);

            if (strcmp(buffer+1, END_OF_FILE) == 0) {
                // we have received the signal that all packets have been sent
                printf("Received the EOF signal\n");
                if (*buffer == last_ack) {
                    // we have received and acknowledged all packets
                    printf("Received and ack'ed all packets\n");
                    break;
                } else {
                    // TODO: Need to do something here???
                    break;
                }
            }
            else if (*buffer < 'A' || (*buffer > 'A' + 2*WINDOW_SIZE+1)) {
                // we have received a packet with a garbled identifier
                printf("Received an unidentified packet: %d\n", *buffer - 'A');

            }
            else if (*buffer == last_ack) {
                // we have received the same packet twice in a row
                printf("Received the same packet twice in a row: %d\n", *buffer - 'A');
                sendto(sockfd, buffer, HEADER_SIZE, 0, (struct sockaddr*)&serveraddr, len);

            } else if (*buffer == last_ack+1 || ((*buffer == 'A') && (last_ack == 'A'+(2*WINDOW_SIZE)-1))) {
                // we have received the next packet
                printf("Received the next packet: %d\n", *buffer - 'A');
                // get the checksum for this packet
                // uint16_t checksumAnswer = checksumCalculated(buffer, len);
                uint16_t checksumAnswer = checksumCalculated(buffer, len);
                printf("Checksum: %d\n", checksumAnswer);
                if (checksumAnswer == 65536) {
                  // packet is not corrupted
                  // send acknowledgement
                } // else request that the packet is resent

                last_ack = *buffer; // set our last acknowledgement to this packet
                // send acknowledgement (need to do error checking before this)
                sendto(sockfd, buffer, HEADER_SIZE, 0, (struct sockaddr*)&serveraddr, len); // send the first byte of the buffer

                // write to the file
                fwrite(buffer+HEADER_SIZE, 1, packet_length - 1, file_out);

                if ((*buffer) == (char)('A'+(2*WINDOW_SIZE)+1)) {
                    printf("Received all packets\n");
                    // the header indicates this is the last packet
                    //
                    // we really can't just break out, since we need to do some final error checking,
                    // but we can do this temporarily
                    break;
                }

            } else if   (((*buffer > 'A'+window_min) && (*buffer <= 'A'+window_max)) ||
                        ((window_min > window_max) && ((*buffer <= 'A' + window_max) || *buffer > 'A' + window_min))) {
                // either our window is in the middle of the window range, and our packet is inside it
                // or our window overlaps back to the beginning (7,8,9,0,1) and our packet is inside it.
                // we have received a packet that we cannot ack yet
                printf("Received a future packet: %d\n", *buffer - 'A');
                memcpy((stored_packet_data+((*buffer-'A') * PACKET_SIZE)), buffer+HEADER_SIZE, strlen(buffer)-1);
                printf("Storing packet data at entry: %d\n", (*buffer-'A') * PACKET_SIZE);
                // this should copy the data from the buffer to the 'slot' of storage in the
                // stored_packet_data string. Packet 'A' will be stored from 0->1023, 'B'
                // from 1024->2047, packet 'Z' at ('Z' - 'A') * 1024

            } else {
                // we have received a past packet
                // this is probably because our ack sent to the server was lost,
                // so lets just resend the ack so the server knows we got the packet
                printf("Received past data packet: %d\n", *buffer - 'A');
                sendto(sockfd, buffer, HEADER_SIZE, 0, (struct sockaddr*)&serveraddr, len);
            }
        }
    }

    close(sockfd);
    free(stored_packet_data);
}
