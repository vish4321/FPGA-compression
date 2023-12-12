#ifndef SERVER_H_
#define SERVER_H_

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>

#define HEADER 2
// max number of elements we can get from ethernet
#define NUM_ELEMENTS 16384

#define FILE_PATH         "/media/sd-mmcblk0p1/"

#define INPUT_FILE          FILE_PATH "LittlePrince.txt"
#define ENCODER_OUT_FILE    FILE_PATH "en_out.txt"
#define DECODER_OUT_FILE    FILE_PATH "decode_out.txt"
#define CHUNK_FILE          FILE_PATH "chunked_data.txt"

class ESE532_Server {
public:

	//
	int setup_server(int avg_blocksize);

	//
	int get_packet(unsigned char* buffer);

protected:

	//
	int sockfd;

	// blocksize passed in from cli
	int blocksize;

	// addresss information
	struct sockaddr_in servaddr;

	//
	socklen_t server_len = sizeof(servaddr);

	int packets_read;

};

#endif
