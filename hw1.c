#include "../unpv13e-master/lib/unp.h" // change according to directory
#include <stdio.h> // support I/O
#include <stdlib.h> // for atoi

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



	// cleanup
	close(sockfd);
	return 0;
}