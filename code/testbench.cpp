#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <iostream>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <iterator>
#include <fstream>

#include "server.h"
#include "stopwatch.h"

stopwatch cdc_timer;
stopwatch dedup_timer;
stopwatch lzw_timer;
stopwatch sha_timer;
stopwatch top_func_timer;

bool Compare_matrices(unsigned char *ch_hw, std::vector<unsigned char> &ch_sw, int size)
{
	bool fail = 0;

    for (int X = 0; X < size; X++) {
      if (ch_hw[X] != ch_sw[X])
      {
        std::cout << "Data not match at " << X << std::endl;
        fail = 1;
        break;
      }
    }

    return fail;
}

int main()
{
  	int length = 0;                      // stores the leength of the incoming packet
    volatile int done = 0;               // indicates whether the packet recived is the last packet or not
    std::vector<hashtable_t> sha_table;  // start the hashtable to store sha values and chunk nukmbers 
    
    int fail;
    unsigned char input[PACKET_SIZE + HEADER];
    FILE *encode_fp = fopen(ENCODER_OUT_FILE, "w"); 

    ESE532_Server server;
    server.setup_server();

    do {

        if(sha_table.size() > 100000){
            // clear the hash table if the size goes above 100,000 chunks 
            sha_table.clear();
        }

        // get packet and store it in the input[] buffer
		server.get_packet(&input[0]);

        // check for length and done bit 
		done = (input[1] & DONE_BIT_L) >> 7;
		length = input[0] | (input[1] << 8);
		length &= ~DONE_BIT_H;
        
        //only call cdc if length is not 0
        if(length != 0){
            unsigned char *in_buf = input + HEADER;
            cdc(in_buf, length, sha_table, encode_fp, done);
            memset(input,0,PACKET_SIZE);
        }
        else{
            done = 1;
        }
   
    } while (!done);

	fclose(encode_fp);
	decoder(ENCODER_OUT_FILE, DECODER_OUT_FILE);
    return 0;
}

