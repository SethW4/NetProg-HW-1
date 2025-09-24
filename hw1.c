#include "../unpv13e-master/lib/unp.h" // change according to directory
#include <stdio.h> // support I/O
#include <stdlib.h> // for atoi

#define MAXBUF 516 // 4 bytes header + 512 bytes data
#define DATA_SIZE 512

// TFTP opcodes
// [Page 4] of RFC 1350
#define OP_RRQ 1
#define OP_WRQ 2
#define OP_DATA 3
#define OP_ACK 4
#define OP_ERROR 5

// Error codes
// [Page 9] of RFC 1350
#define ERR_NOT_DEFINE 0
#define ERR_FILE_NOT_FOUND 1
#define ERR_ACCESS_VIOLATION 2
#define ERR_DISK_FULL 3
#define ERR_ILLEGAL_OPERATION 4
#define ERR_UNKNOWN_TID 5
#define ERR_FILE_EXISTS 6
#define ERR_NO_SUCH_USER 7

int main(int argc, char **argv) {
	if (argc != 3) { // outfile + two arguments
		perror("ERROR: Two arguments allowed only\n");
		exit(EXIT_FAILURE);
	}

	int lowestPort = atoi(argv[1]); // used instead of 69
	int highestPort = atoi(argv[2]);
	if (lowestPort <= 0 || highestPort <= 0 || highestPort < lowestPort) {
		perror("ERROR: Invalid port range\n");
		exit(EXIT_FAILURE);
	}
	int nextTidPort = lowestPort + 1;
	if (nextTidPort > highestPort) {
		perror("ERROR: insufficient port range\n");
		exit(EXIT_FAILURE);
	}

	int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sockfd < 0) {
		perror("ERROR: socket() failed\n");
		exit(EXIT_FAILURE);
	}

	struct sockaddr_in server_addr;
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port = htons(lowestPort);
	if (bind(sockfd, (struct sockaddr*) &server_addr, sizeof(server_addr)) < 0) {
		perror("ERROR: bind() failed");
		exit(EXIT_FAILURE);
	}

	for (;;) {
		unsigned char buf[MAXBUF];
		struct sockaddr_in from_addr;
		socklen_t from_len = sizeof(from_addr);

		ssize_t n = recvfrom(sockfd, buf, sizeof(buf) - 1, 0,
				    	    (struct sockaddr *) &from_addr, &from_len);
		if (n < 0) {
			perror("recvfrom() failed");
			continue;
		}
		if (n < 2) continue;

		unsigned short opcode;
		memcpy(&opcode, buf, 2);
		opcode = ntohs(opcode);

		if(opcode != OP_RRQ && opcode != OP_WRQ) {
			// TODO: handle send error
			continue;
		}

		// assign TID port
		int assigned = -1;
		for (int i = nextTidPort; i <= highestPort; ++i) {
			// TODO: assign next TID port and track used ports
		}
		if (assigned == -1) { // no free port
			// TODO: handle no free port error
			continue;
		}

		pid_t pid = fork();
		if (pid < 0) {
			perror("ERROR: fork() failed");
			// TODO: send to server
			continue;
		}
		else if (pid == 0) { // child process
			close(sockfd);
			// TODO
			exit(EXIT_SUCCESS);
		}
		else { // parent process
			// TODO
		}
	}

	// cleanup
	close(sockfd);
	return 0;
}