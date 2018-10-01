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
char END_OF_FILE[4] = "EOF\0";
int sendNextPacket(char* read_buffer, FILE* file_ptr, int *pack_ID, long file_length, int sockfd, struct sockaddr_in clientaddr, uint len);
void addChecksum(char* buffer);
int main(int argc, char** argv) {
	int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if(sockfd < 0) {
		printf("There was an ERROR(1) creating the socket\n");
		return 1;
	}

	int portNum;

	struct timeval timeout;
	timeout.tv_sec = 0;
	timeout.tv_usec = 250000;

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
			sendto(sockfd, client_response, n, 0, (struct sockaddr*)&clientaddr, len);
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
			int last_ack_needed = -1;


			printf("Sending first 5 packets\n");
			for (; current_packet < WINDOW_SIZE; current_packet++) {
				sendNextPacket(read_buffer, in, &pack_ID, file_length, sockfd, clientaddr, len);
			}


			while (1) {
				int window_min = (current_ack_needed - 'A' + 1) % (2*WINDOW_SIZE);
            	int window_max = (window_min+WINDOW_SIZE-1) % (2*WINDOW_SIZE);

				if (last_ack_needed != -1) {
					// TODO: Need to figure out why server isn't stopping
					// once the client receives all packets. is the final
					// EOF packet getting lost? Is last_ack_needed not
					// getting modified?
				}
				if (client_response[0] == last_ack_needed) {
					// we have received confirmation that the last packet has been
					// written to file, so lets close up shop
					printf("Received confirmation that the last packet has been written\n");
					return 0;
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

						sendNextPacket(read_buffer, in, &pack_ID, file_length, sockfd, clientaddr, len);
					} else {
						break;
					}

				}

				if (recvfrom(sockfd, client_response, PACKET_SIZE, 0, (struct sockaddr*)&clientaddr, &len) < 0) {
					// We did not receive any data from the client...
					// either they disconnected or a packet was lost. If we have any more
					// data to send, lets resend the first packet in our window

					printf("Did not receive data from the client\n");
					int ftell_before_seek = ftell(in);

					int packet_difference = pack_ID - current_ack_needed;
					if (packet_difference < 0) {
						packet_difference += 2*WINDOW_SIZE;
					}
					//printf("Packet we want to go to: %d at byte %d\n", current_ack_needed, packet_difference);
					printf("ftell: %lu\n", ftell(in));
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

					//memset(acks, '0', sizeof(acks));

				} else {
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
						if (client_response[0] >= current_ack_needed + WINDOW_SIZE ||
							window_max < window_min && client_response[0] <= 'A' + window_max) {
							// this ack is in our window range, but we it is not the next
							// one we need, so lets store it for later
							printf("Storing ack: %d\n", client_response[0]-'A');
							acks[client_response[0]-'A'] = '1';
						} else {
							// received a byte that is not in our window range
						}
					}
				}
			}
		}
	}
}

int checksumCalculated(char* buffer, size_t len) {
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
//  printf("Checksum prior to: %zu\n", sum);
  // gets 1s compliment
  return ~(sum & 0xFFFF);
}

int sendNextPacket(char* read_buffer, FILE* file_ptr, int *pack_ID, long file_length, int sockfd, struct sockaddr_in clientaddr, uint len) {
	*(read_buffer) = (char)('A'+(*pack_ID)); // add the identifier to the beginning of the packet
	// this identifier will be a char, starting at A, and ending at A + WINDOW_SIZE*2
	// this will end up being, in the case of the window size being 5, A through J

	int diff = file_length - ftell(file_ptr); // amount of data we have left
	int actual_packet_size = PACKET_SIZE;
	printf("%lu bytes in, %d bytes left\n", ftell(file_ptr), diff);
	if (diff == 0) {
		// the file has no more data to send, so let the client
		// know that we have no more packets to send. Let's use
		// a constant string as a signal to close the connection
		*(read_buffer) = (char)('A'+(*pack_ID-1));
		strcpy(read_buffer+1, END_OF_FILE);
		sendto(sockfd, read_buffer, sizeof(END_OF_FILE)+1, 0, (struct sockaddr*)&clientaddr, len);
		return 1;
	}

  // add checksum to the packet
  // fits checksum into 2 bytes
  // puts first 8 bytes of checksum in 2nd bit of packet
  int answer = abs(checksumCalculated(read_buffer, len));
  printf("Checksum: %d\n",answer);
  int checksumPartition = sqrt(answer);
  *(read_buffer + 1) = (char)(checksumPartition);
  *(read_buffer + 2) = (char)(checksumPartition + 1);
  printf("Checksum Bit 1: %d\n",checksumPartition);
  printf("Checksum Bit 2: %d\n",checksumPartition + 1);
  printf("Char 1: %c\n",(char)checksumPartition);
  printf("Char 2: %c\n",(char)(checksumPartition +1));
  printf("\n");
  printf("%s\n", read_buffer);



  // puts second 8 bytes of checksum in 3rd bit of packet
  if (checksumPartition %2 != 0) {
    *(read_buffer + 2) = (char)(checksumPartition + 1);
  } else {
    *(read_buffer + 2) = (char)(checksumPartition);
  }
  // printf("%s\n", read_buffer);
	if (diff <= PACKET_SIZE) {
		fread(read_buffer+1, 1, diff, file_ptr);
		// we have less than PACKET_SIZE bytes left in the file, so only
		// read 'diff' number of bytes of the file into the read_buffer.
		//
		// this will seek to the end of the file
		actual_packet_size = diff;
	} else {
		fread(read_buffer+1, sizeof(char), PACKET_SIZE, file_ptr);
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
