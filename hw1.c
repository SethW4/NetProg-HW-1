//#include "../unpv13e-master/lib/unp.h" // change according to directory
#include "../lib/unp.h"
#include <stdio.h> // support I/O
#include <stdlib.h> // for atoi
#include <sys/wait.h> // for waitpid 

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

#define MAX_PORTS 65536

#define TIMEOUT 1 // 1 second retransmit
#define ABORT 10 // 10 seconds max without response

#define MAX_CHILDREN 1024 // It SHOULDN'T need more than that 

// For tracking which ports have been used  
static int usedPorts[MAX_PORTS];  // 0 = free, 1 = used

static ssize_t last_sent_len = 0;
static unsigned char last_packet[MAXBUF];
static struct sockaddr_in last_client_addr;
static socklen_t last_client_len;
static int last_sock = -1;
static int elapsed = 0;


typedef struct {
	pid_t pid;
	int port;
} child_info;


child_info children[MAX_CHILDREN];
int num_children = 0;

void sigalrm_handler(int signo) {
    if (elapsed >= ABORT) {
        perror("ERROR: Elapsed time greater than 10 seconds. Aborting transfer\n");
        exit(EXIT_FAILURE);
    }
    if (last_sent_len > 0 && last_sock != -1) {
        sendto(last_sock, last_packet, last_sent_len, 0, (struct sockaddr *)&last_client_addr, last_client_len);
    }
    elapsed += TIMEOUT;
    alarm(TIMEOUT); // schedule next alarm
}

void setup_signal_handlers() {
    struct sigaction sa;
    sa.sa_handler = sigalrm_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGALRM, &sa, NULL);
}

int send_error(int sock, const struct sockaddr *client_addr, socklen_t addr_len, int error_code, const char *error_msg)
{
    char buffer[MAXBUF];  
    size_t msg_len = strlen(error_msg);

    uint16_t *opcode_field = (uint16_t *)buffer;
    uint16_t *errcode_field = (uint16_t *)(buffer + 2);

    *opcode_field = htons(OP_ERROR);
    *errcode_field = htons(error_code);

    memcpy(buffer + 4, error_msg, msg_len);
    buffer[4 + msg_len] = '\0'; 

    size_t packet_len = 4 + msg_len + 1;

    ssize_t sent = sendto(sock, buffer, packet_len, 0, client_addr, addr_len);

    if (sent < 0) {
        perror("ERROR: sendto failed for the ERROR packet");
        return -1;
    }

    return 0;
}

void handle_rrq(FILE *fp, int sock, struct sockaddr_in *client_addr, socklen_t client_len) {
    unsigned short block = 1;
    size_t nread;
    unsigned char buf[MAXBUF];

    last_sock = sock;
    last_client_addr = *client_addr;
    last_client_len = client_len;

    while (1) {
        // read up to 512 bytes
        nread = fread(buf + 4, 1, DATA_SIZE, fp);

        // build DATA packet
        uint16_t *opcode_field = (uint16_t *)buf;
        uint16_t *block_field = (uint16_t *)(buf + 2);
        *opcode_field = htons(OP_DATA);
        *block_field = htons(block);

        size_t packet_len = 4 + nread;
        memcpy(last_packet, buf, packet_len); // save for retransmit
        last_sent_len = packet_len;
        elapsed = 0;
        alarm(TIMEOUT); // start timer

        ssize_t sent = sendto(sock, buf, packet_len, 0, (struct sockaddr *)client_addr, client_len);
        if (sent < 0) { perror("ERROR: sendto() failed"); exit(EXIT_FAILURE); }

        // wait for ACK
        unsigned char ack_buf[MAXBUF];
        while (1) {
            ssize_t n = recvfrom(sock, ack_buf, sizeof(ack_buf), 0, NULL, NULL);
            if (n < 0) {
                if (errno == EINTR) continue; // if interrupted by SIGALRM then retransmit
                perror("ERROR: recvfrom() failed"); exit(EXIT_FAILURE);
            }
            // check ACK
            uint16_t ack_opcode, ack_block;
            memcpy(&ack_opcode, ack_buf, 2);
            memcpy(&ack_block, ack_buf + 2, 2);
            ack_opcode = ntohs(ack_opcode);
            ack_block  = ntohs(ack_block);

            if (ack_opcode == OP_ACK && ack_block == block) {
                // correct ACK received
                break;
            }
            // otherwise ignore and wait 
        }

        // stop timer
        alarm(0);
        last_sent_len = 0;

        if (nread < DATA_SIZE) 
        {
        	// last packet sent
        	break; 
        }
        block++;
    }
}

void handle_wrq(FILE *fp, int sock, struct sockaddr_in *client_addr, socklen_t client_len) {
    unsigned short block = 0; // start with ACK block 0
    unsigned char ack_buf[MAXBUF];

    last_sock = sock;
    last_client_addr = *client_addr;
    last_client_len = client_len;
    elapsed = 0;

    // Send initial ACK block 0
    uint16_t opcode = htons(OP_ACK);
    uint16_t blknum = htons(block);
    memcpy(ack_buf, &opcode, 2);
    memcpy(ack_buf + 2, &blknum, 2);

    size_t packet_len = 4;
    memcpy(last_packet, ack_buf, packet_len);
    last_sent_len = packet_len;
    alarm(TIMEOUT);

    if (sendto(sock, ack_buf, packet_len, 0, (struct sockaddr *)client_addr, client_len) < 0) {
        perror("ERROR: sendto() failed"); exit(EXIT_FAILURE);
    }

    while (1) {
        // Wait for DATA packet
        unsigned char data_buf[MAXBUF];
        ssize_t n = recvfrom(sock, data_buf, sizeof(data_buf), 0, NULL, NULL);
        if (n < 0) {
            if (errno == EINTR) continue; // if interrupted by SIGALRM then retransmit last ACK
            perror("ERROR: recvfrom() failed"); exit(EXIT_FAILURE);
        }

        // Parse DATA packet
        uint16_t data_opcode, data_block;
        memcpy(&data_opcode, data_buf, 2);
        memcpy(&data_block, data_buf + 2, 2);
        data_opcode = ntohs(data_opcode);
        data_block  = ntohs(data_block);

        if (data_opcode != OP_DATA) {
            // Illegal operation
            send_error(sock, (struct sockaddr *)client_addr, client_len, ERR_ILLEGAL_OPERATION, "ERROR: Expected DATA packet");
            exit(EXIT_FAILURE);
        }

        if (data_block == block + 1) {
            // Correct next block
            size_t data_len = n - 4;
            if (fwrite(data_buf + 4, 1, data_len, fp) != data_len) {
                perror("ERROR: fwrite() failed"); 
                send_error(sock, (struct sockaddr *)client_addr, client_len, ERR_DISK_FULL, "ERROR: Write failed");
                exit(EXIT_FAILURE);
            }

            // increment expected block
            block++; 

            // Send ACK
            uint16_t ack_opcode = htons(OP_ACK);
            uint16_t ack_block = htons(block);
            memcpy(ack_buf, &ack_opcode, 2);
            memcpy(ack_buf + 2, &ack_block, 2);
            last_sent_len = 4;
            memcpy(last_packet, ack_buf, 4);
            elapsed = 0;
            alarm(TIMEOUT);

            if (sendto(sock, ack_buf, 4, 0, (struct sockaddr *)client_addr, client_len) < 0) {
                perror("ERROR: sendto() failed"); exit(EXIT_FAILURE);
            }

            if (data_len < DATA_SIZE) 
            {
            	// last packet 
            	break; 
        	}
        }
        else if (data_block <= block) {
            // Duplicate DATA packet, re-ACK
            uint16_t ack_opcode = htons(OP_ACK);
            uint16_t ack_block = htons(data_block);
            memcpy(ack_buf, &ack_opcode, 2);
            memcpy(ack_buf + 2, &ack_block, 2);
            last_sent_len = 4;
            memcpy(last_packet, ack_buf, 4);
            elapsed = 0;
            alarm(TIMEOUT);

            sendto(sock, ack_buf, 4, 0, (struct sockaddr *)client_addr, client_len);
        }
        else {
            // Out-of-order block, ignore it 
        }
    }

    alarm(0);
    last_sent_len = 0;
}



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
			// Sends ERROR back maybe? 
			send_error(sockfd, (struct sockaddr *)&from_addr, from_len, ERR_ILLEGAL_OPERATION, "ERROR: Illegal TFTP operation");
			continue;
		}

		// assign TID port
		int assigned = -1;
		int tid_sock = -1; 
		for (int i = nextTidPort; i <= highestPort; ++i) {
			if (!usedPorts[i]) {
		        tid_sock = socket(AF_INET, SOCK_DGRAM, 0);
		        if (tid_sock < 0) {
		            perror("ERROR: socket() failed");
		            continue;
		        }

		        struct sockaddr_in tid_addr;
		        memset(&tid_addr, 0, sizeof(tid_addr));
		        tid_addr.sin_family = AF_INET;
		        tid_addr.sin_addr.s_addr = htonl(INADDR_ANY);
		        tid_addr.sin_port = htons(i);

		        if (bind(tid_sock, (struct sockaddr *)&tid_addr, sizeof(tid_addr)) < 0) {
		        	// port not available 
		            perror("ERROR: bind() failed");
		            close(tid_sock);
		            continue; 
		        }

		        // success 
		        usedPorts[i] = 1;
		        assigned = i;
		        if (i == highestPort) {
				    nextTidPort = lowestPort;
				} else {
				    nextTidPort = i + 1;
				}
		        break;
		    }
		}
		if (assigned == -1) { // no free port
			send_error(sockfd, (struct sockaddr *)&from_addr, from_len, ERR_NOT_DEFINE, "ERROR: No free TID ports available");
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
			
			// Parse request
		    char *filename = (char *)(buf + 2);
		    char *mode = filename + strlen(filename) + 1;

		    if (strcasecmp(mode, "octet") != 0) {
		        send_error(tid_sock, (struct sockaddr *)&from_addr, from_len, ERR_ILLEGAL_OPERATION, "ERROR: Only octet mode supported");
		        close(tid_sock);
		        usedPorts[assigned] = 0;
		        exit(EXIT_FAILURE);
		    }

		    if (opcode == OP_RRQ) {
		        // open file for reading
		        FILE *fp = fopen(filename, "rb");
		        if (!fp) {
		            send_error(tid_sock, (struct sockaddr *)&from_addr, from_len, ERR_FILE_NOT_FOUND, "ERROR: File not found");
		            close(tid_sock);
		            usedPorts[assigned] = 0;
		            exit(EXIT_FAILURE);
		        }

		        // TODO: setup retransmission handler with SIGALRM
		        setup_signal_handlers();

		        // send DATA blocks, wait for ACKs, retransmit on timeout...
		        handle_rrq(fp, tid_sock, &from_addr, from_len);

		        fclose(fp);
		    }
		    else if (opcode == OP_WRQ) {
		        // open file for writing (fail if it exists)
		        FILE *fp = fopen(filename, "rb");
		        if (fp) {
		            fclose(fp);
		            send_error(tid_sock, (struct sockaddr *)&from_addr, from_len, ERR_FILE_EXISTS, "ERROR: File already exists");
		            close(tid_sock);
		            usedPorts[assigned] = 0;
		            exit(EXIT_FAILURE);
		        }
		        fp = fopen(filename, "wb");
		        if (!fp) {
		            send_error(tid_sock, (struct sockaddr *)&from_addr, from_len, ERR_ACCESS_VIOLATION, "ERROR: Cannot create file");
		            close(tid_sock);
		            usedPorts[assigned] = 0;
		            exit(EXIT_FAILURE);
		        }

		        // TODO 
		        setup_signal_handlers();

		        // TODO: ACK block #0, then receive DATA, send ACKs 
		        handle_wrq(fp, tid_sock, &from_addr, from_len);

		        fclose(fp);
		    }

		    close(tid_sock);
		    usedPorts[assigned] = 0;   
			exit(EXIT_SUCCESS);
		}
		else { // parent process
			// TODO???
			close(tid_sock); // not needed by parent 

			// Track child PID and assigned port
		    if (num_children < MAX_CHILDREN) {
		        children[num_children].pid = pid;
		        children[num_children].port = assigned;
		        num_children++;
		    }

		    // Reap any finished children and free their ports
		    for (int i = 0; i < num_children; ++i) {
		        int status;
		        pid_t ret = waitpid(children[i].pid, &status, WNOHANG);
		        if (ret > 0) { // child exited
		            usedPorts[children[i].port] = 0; // free the port
		            // remove from array
		            children[i] = children[num_children - 1];
		            num_children--;
		            i--; // check new child in this position
		        }
		    }

		}
	}

	// cleanup
	close(sockfd);
	return 0;
}