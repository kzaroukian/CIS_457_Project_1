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
char END_OF_FILE[4] = "EOF\0";
int TIMEOUT_MAX_ATTEMPTS = 10;
int sendNextPacket(char* read_buffer, FILE* file_ptr, int *pack_ID, long file_length, int sockfd, struct sockaddr_in clientaddr, uint len);
void addChecksum(char* buffer);
int main(int argc, char** argv) {
	int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if(sockfd < 0) {
		printf("Failed to create the socket\n");
		return 1;
	}

	int portNum;

	struct timeval timeout;
	timeout.tv_sec = 3;
	timeout.tv_usec = 0;

	setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

	int port = 9999;
	/*
	printf("Enter a port: ");
	scanf("%d", &port);
	printf("\n");
	getchar();
	*/
	struct sockaddr_in serveraddr, clientaddr;
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_port = htons(port); 
	serveraddr.sin_addr.s_addr = INADDR_ANY;

	int b = bind(sockfd, (struct sockaddr*)&serveraddr, sizeof(serveraddr)); //associates address with socket 
	if(b < 0){
		printf("Binding failed\n");
		return 2;
	}

	char *read_buffer = (char*) malloc((PACKET_SIZE + HEADER_SIZE) * sizeof(char));

	while(1){
		uint len = sizeof(clientaddr);
		char client_response[PACKET_SIZE];
		int n = recvfrom(sockfd, client_response, PACKET_SIZE, 0, (struct sockaddr*)&clientaddr, &len);

		if(n < 0)
			printf("Waiting...\n");
		else {
			printf("Filename received: %s\n", client_response);
			sendto(sockfd, client_response, n, 0, (struct sockaddr*)&clientaddr, len);
			FILE * in = fopen (client_response, "r");
			if (in == NULL) {
				printf("Error: File <%s> not found\n", client_response);
				memset(client_response, 0, 1);
				printf("----> %d\n", client_response[0]);
				sendto(sockfd, client_response, 1, 0, (struct sockaddr*)&clientaddr, len);
				// send a NULL byte to denote an error occurred
				continue;
			}
			fseek(in, 0L, SEEK_END);
			long file_length = ftell(in);
			fseek(in, 0, SEEK_SET);

			int pack_ID = 0;
			int current_packet = 0;
			char acks[WINDOW_SIZE*2];
			int current_ack_needed = 0;
			int last_ack_needed = -1;

			int timeout_counter = 0; // holds the number of times our packet has failed to send

			timeout.tv_sec = 0;
			timeout.tv_usec = 250000;
			setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

			printf("Sending first 5 packets\n");
			for (; current_packet < WINDOW_SIZE; current_packet++) {
				if (sendNextPacket(read_buffer, in, &pack_ID, file_length, sockfd, clientaddr, len) == 1) {
					last_ack_needed = pack_ID-1;
					if (last_ack_needed < 0) {
						last_ack_needed = last_ack_needed + (2 * WINDOW_SIZE);
					}
					printf("Sent the EOF packet: %d\n", last_ack_needed);
					break;
				}
			}


			while (1) {
				int window_min = (current_ack_needed + 1) % (2*WINDOW_SIZE);
            	int window_max = ((window_min + WINDOW_SIZE) - 1) % (2*WINDOW_SIZE);

            	int i = 0;
            	for (; i < strlen(acks); i++) {
            		if (((i < window_min || i > window_max) && window_min < window_max) ||
            			((i < window_min && i > window_max) && window_min > window_max)) {

            			// if i is NOT in our window
            			acks[i] = '0';
            		}
            	}

				if ((client_response[0] == 'A'+last_ack_needed && strcmp(client_response+1, END_OF_FILE) == 0)
					|| (last_ack_needed >= 0 && acks[last_ack_needed] == '1')) {
					// we have received confirmation that the last packet has been
					// written to file, so lets close up shop
					printf("Received confirmation that the last packet has been written\n");
					break;
				}
				printf("current ack needed: %d\n", current_ack_needed);
				while (1) {
					if (acks[current_ack_needed] == '1') {
						// already have the ack that we need
						acks[current_ack_needed] = '0';
						printf("Already have the needed ack in storage: %d\n", current_ack_needed);

						current_ack_needed++;
						if (current_ack_needed >= sizeof(acks)) {
							current_ack_needed = 0;
						}

						if (sendNextPacket(read_buffer, in, &pack_ID, file_length, sockfd, clientaddr, len) == 1) {
							last_ack_needed = pack_ID-1;
							if (last_ack_needed < 0) {
								last_ack_needed = last_ack_needed + (2 * WINDOW_SIZE);
							}
							printf("Sent the EOF packet: %d\n", last_ack_needed);
						}
					} else {
						break;
					}
					
				}

				if (recvfrom(sockfd, client_response, PACKET_SIZE, 0, (struct sockaddr*)&clientaddr, &len) < 0) {
					// We did not receive any data from the client...
					// either they disconnected or a packet was lost. If we have any more
					// data to send, lets resend the first packet in our window
					timeout_counter++;
					printf("Timeout: %d\n", timeout_counter);
					if (timeout_counter >= TIMEOUT_MAX_ATTEMPTS) {
						timeout_counter = 0;
						printf("Client disconnected...\n");
						break;
					}


					int ftell_before_seek = ftell(in);

					int packet_difference = pack_ID - current_ack_needed;
					if (packet_difference < 0) {
						packet_difference += 2*WINDOW_SIZE;
					}

					if (ftell(in) >= file_length) {
						fseek(in, (-PACKET_SIZE*(packet_difference-1)) - (ftell(in) % PACKET_SIZE), SEEK_CUR);
					} else {
						fseek(in, -PACKET_SIZE*packet_difference, SEEK_CUR);
					}

					// TODO: Can we somehow change current_ack_needed to the last packet that we sent
					// BEFORE the resending of the first packet
					
					int temp_pack_ID = pack_ID;
					pack_ID = current_ack_needed;
					
					//current_packet = 0;
					//for (; current_packet < WINDOW_SIZE; current_packet++) {
						if (sendNextPacket(read_buffer, in, &pack_ID, file_length, sockfd, clientaddr, len) == 1) {
							last_ack_needed = pack_ID-1;
							if (last_ack_needed < 0) {
								last_ack_needed = last_ack_needed + (2 * WINDOW_SIZE);
							}
							printf("Sent the EOF packet: %d\n", last_ack_needed);
					//		break;
						}
					//}
					pack_ID = temp_pack_ID;
					fseek(in, ftell_before_seek, SEEK_SET);
					// this resets the file pointer back to where we were before we rewound

				} else {
					timeout_counter = 0;
					printf("ack received: %d\n", client_response[0] - 'A');
					if (client_response[0] < 'A' || client_response[0] > 'A' + (2*WINDOW_SIZE)) {
						printf("We have received a garbled identifier: %d\n", client_response[0] - 'A');

					} else if (client_response[0] - 'A' == last_ack_needed) {
						// we have received confirmation that the last packet has been
						// written to file, so lets close up shop
						printf("Received confirmation that the last packet has been written\n");
						break;

					} else if (client_response[0] == 'A' + current_ack_needed) {
						// this is the ack that we need, so send the next packet
						// TODO: EOF packet is getting sent multiple times, maybe
						// there's a way to set a flag so it doesn't get sent every
						// time the next ack comes in?
						if (sendNextPacket(read_buffer, in, &pack_ID, file_length, sockfd, clientaddr, len)) {
							// if this returns 1, we sent the EOF packet
							last_ack_needed = pack_ID-1;
							if (last_ack_needed < 0) {
								last_ack_needed = last_ack_needed + (2 * WINDOW_SIZE);
							}
							printf("Sent the EOF packet: %d\n", last_ack_needed);
							acks[current_ack_needed] = '0';
							current_ack_needed++;
							if (current_ack_needed >= sizeof(acks)) {
								current_ack_needed = 0;
							}
							continue;
						}
						acks[current_ack_needed] = '0';
						current_ack_needed++;
						if (current_ack_needed >= sizeof(acks)) {
							current_ack_needed = 0;
						}

					} else {
						printf("Min: %d  Max: %d\n", window_min, window_max);
						if (((client_response[0] - 'A' > window_min && client_response[0] - 'A' <= window_max) && window_min < window_max) ||
							((client_response[0] - 'A' > window_min || client_response[0] - 'A' <= window_max) && window_min > window_max)) {
							// this ack is in our window range, but it is not the next
							// one we need, so lets store it for later
							printf("Storing ack: %d\n", client_response[0]-'A');
							acks[client_response[0]-'A'] = '1';
						} else {
							// received a byte that is not in our window range
						}
					}
				}
			}
			memset(acks, '0', strlen(acks));
			// make sure to reset our stored acks once we are finished with this client
			timeout.tv_sec = 3;
			timeout.tv_usec = 0;
			setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
			// lets reset our rate back to one check every 3 seconds
		}
	}
}

int sendNextPacket(char* read_buffer, FILE* file_ptr, int *pack_ID, long file_length, int sockfd, struct sockaddr_in clientaddr, uint len) {
	*(read_buffer) = (char)('A'+(*pack_ID)); // add the identifier to the beginning of the packet
	// this identifier will be a char, starting at A, and ending at A + WINDOW_SIZE*2
	// this will end up being, in the case of the window size being 5, A through J
	
	int diff = file_length - ftell(file_ptr); // amount of data we have left
	int actual_packet_size = PACKET_SIZE;
	//printf("%lu bytes in, %d bytes left\n", ftell(file_ptr), diff);
	if (diff == 0) {
		// the file has no more data to send, so let the client
		// know that we have no more packets to send. Let's use
		// a constant string as a signal to close the connection
		*read_buffer = (char)('A'+(*pack_ID-1));
		if (*read_buffer < 'A') {
			*read_buffer = (char) ('A' + 2*WINDOW_SIZE - 1);
		}
		strcpy(read_buffer+1, END_OF_FILE);
		sendto(sockfd, read_buffer, sizeof(END_OF_FILE)+1, 0, (struct sockaddr*)&clientaddr, len);
		return 1;
	}
	if (diff <= PACKET_SIZE) {
		fread(read_buffer+1, 1, diff, file_ptr);
		// we have less than PACKET_SIZE bytes left in the file, so only
		// read 'diff' number of bytes of the file into the read_buffer.
		// 
		// this will seek to the end of the file
		actual_packet_size = diff;
	} else {
		fread(read_buffer+1, 1, PACKET_SIZE, file_ptr);
		// read PACKET_SIZE bytes of the file into the read_buffer

	}
	
	printf("Sending packet ID -> %d(%d bytes)\n", *pack_ID, actual_packet_size);

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

void addChecksum(char* buffer) {

}