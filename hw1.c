#include "../unpv13e-master/lib/unp.h" // change according to directory
#include <stdio.h> // support I/O
#include <stdlib.h> // for atoi

int main(int argc, char **argv) {
	if (argc != 3) { // outfile + two arguments
		perror("ERROR: Two arguments allowed only");
		exit(EXIT_FAILURE);
	}

	int lowestPort = atoi(argv[1]); // used instead of 69
	int highestPort = atoi(argv[2]);


	return 0;
}