#include <sys/socket.h>
#include <netinet/in.h> 
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
int checksum(char* packet_data);
int main(int argc, char** argv) {
    int WINDOW_SIZE = 5; // we can handle 5 packets at a time
    int HEADER_SIZE = 1; // need 1 byte for identifying packets
    int PACKET_SIZE = 1024; // we can send a max of 1024 bytes (excluding headers)
    char END_OF_FILE[4] = "EOF\0";
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if(sockfd < 0) {
        printf("There was an ERROR(1) creating the socket\n");
        return 1;
    }

    struct timeval timeout;
    timeout.tv_sec = 1;
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
    char ip_address[20] = "10.0.0.2";


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

    sendto(sockfd, file_name, strlen(file_name)+1, 0, (struct sockaddr*)&serveraddr, len);
    char file_name_received[PACKET_SIZE];
    while (1) {
        if (recvfrom(sockfd, file_name_received, PACKET_SIZE, 0, (struct sockaddr*)&serveraddr, &len) < 0) {
            printf("Filename packet was lost, resending...\n");
            sendto(sockfd, file_name, strlen(file_name)+1, 0, (struct sockaddr*)&serveraddr, len);
        } else {
            if (file_name_received[0] == 0) {
                printf("File not found on server\n");
                return 1;
            }
            break;
        }
    }

    // create our output file
    FILE *file_out = fopen(file_write_name, "w");

    timeout.tv_sec = 0;
    timeout.tv_usec = 100000;

    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    char last_ack = 'A'-1;
    int stored_packet_data_len = PACKET_SIZE * 2 * WINDOW_SIZE;

    char *stored_packet_data = (char*) malloc(stored_packet_data_len * sizeof(char));
    int stored_packet_data_sizes[2*WINDOW_SIZE];
    
    memset(stored_packet_data, 0, PACKET_SIZE * 2 * WINDOW_SIZE);
    memset(stored_packet_data_sizes, 0, 2 * WINDOW_SIZE);
    int packet_length;
    int final_packet = -1;
    while (1) {
        // before we receive any data, check if we already have the next packet we need
        // stored into memory. If so, lets write it to the file and send the ack
        int packet_index = (PACKET_SIZE * ((last_ack-'A')+1)) % stored_packet_data_len;

        if (*(stored_packet_data + packet_index) != 0) {
            while (1) {
                if (*(stored_packet_data + packet_index) != 0) {
                    last_ack ++;
                    if (last_ack >= 'A' + (2*WINDOW_SIZE)) {
                        last_ack = 'A';
                    }
                    printf("Writing stored packet data: %d at %d\n", (last_ack-'A'), packet_index);
                    // write to the file

                    fwrite(stored_packet_data + packet_index, 1, stored_packet_data_sizes[last_ack-'A'], file_out);
                    printf("data size: %d\n", stored_packet_data_sizes[last_ack-'A']);

                    memset(stored_packet_data + packet_index, 0, PACKET_SIZE);
                    sendto(sockfd, &last_ack, HEADER_SIZE, 0, (struct sockaddr*)&serveraddr, len);
                    stored_packet_data_sizes[last_ack-'A'] = 0;
                    if (final_packet != -1 && (last_ack - 'A' == packet_index / PACKET_SIZE)) {
                        // we have just written the last packet to file and sent the ack
                        printf("Received and ack'ed all packets\n");
                        close(sockfd);
                        free(stored_packet_data);
                        return 0;
                    }

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
            printf("Waiting...\n");
            continue;
        } else {
            int window_min = (last_ack - 'A' + 1) % (2*WINDOW_SIZE);
            int window_max = (window_min+WINDOW_SIZE-1) % (2*WINDOW_SIZE);

            printf("Min: %d, Max: %d\n", window_min, window_max);

            if (strcmp(buffer+1, END_OF_FILE) == 0) {
                // we have received the signal that all packets have been sent
                printf("Received the EOF signal: %d\n", (*buffer) - 'A');
                final_packet = *buffer;
                if (*buffer == last_ack) {
                    // we have received and acknowledged all packets
                    printf("Received and ack'ed all packets\n");
                    break;
                } else {
                    // TODO: Need to do something here???
                    continue;
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
                last_ack = *buffer; // set our last acknowledgement to this packet
                memset(stored_packet_data + ((last_ack-'A') * PACKET_SIZE), 0, PACKET_SIZE);
                // send acknowledgement (need to do error checking before this)
                sendto(sockfd, buffer, HEADER_SIZE, 0, (struct sockaddr*)&serveraddr, len); // send the first byte of the buffer
                
                // write to the file
                fwrite(buffer+HEADER_SIZE, 1, packet_length - 1, file_out);
                //printf("data: %s\n", buffer + HEADER_SIZE);

                if (*buffer == final_packet && final_packet != -1) {
                    // this is the last packet, so let's break out
                    break;
                }

            } else if   (((*buffer > 'A'+window_min) && (*buffer <= 'A'+window_max)) || 
                        ((window_min > window_max) && ((*buffer <= 'A' + window_max) || *buffer > 'A' + window_min))) {
                // either our window is in the middle of the window range, and our packet is inside it
                // or our window overlaps back to the beginning (7,8,9,0,1) and our packet is inside it.
                // we have received a packet that we cannot ack yet
                printf("Received a future packet: %d\n", *buffer - 'A');
                memcpy((stored_packet_data+((*buffer-'A') * PACKET_SIZE)), buffer+HEADER_SIZE, packet_length);
                stored_packet_data_sizes[*buffer - 'A'] = packet_length - 1;

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

int checksum(char* packet_data) {
    return 0;
}

