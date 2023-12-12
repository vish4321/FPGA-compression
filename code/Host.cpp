#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <unistd.h>
#include <vector>
#include <thread>
#include <fcntl.h>
#include <sys/mman.h>
#include <iterator>
#include <mutex>

#include "main.h"
#include "stopwatch.h"

ESE532_Server server;

stopwatch ethernet_timer;
stopwatch out_write_timer;
stopwatch cdc_timer;
extern stopwatch lzw_hw_timer;
extern stopwatch sha_timer;
extern stopwatch dedup_timer;
stopwatch total_timer;

int offset = 0;
unsigned char* file;
unsigned long total_lzw_len = 0;
unsigned int length = 0;
unsigned int packet_counter = 0;
int done = 0;
unsigned char *input = (unsigned char*) malloc(sizeof(unsigned char)*(PACKET_SIZE + HEADER));
std::vector<hashtable_t> sha_table;
volatile int filled = 1;
// Create object for mutex 
std::mutex mtx; 



void Core2_Process( unsigned int &length, int &done, unsigned char *input, volatile int &filled){
        
        //spin lock for thread
	while(filled == 1);
		
	if(done == 0){
		do{
			ethernet_timer.start();
			server.get_packet(&input[0]);
			ethernet_timer.stop();

			done = (input[0] & DONE_BIT_L) >> 7;
			length = input[0] | (input[1] << 8);
			length &= ~DONE_BIT_H;
			
		}while(length == 0);
	}
	// counter for packet length 
	packet_counter +=length;
	
	mtx.lock();
	filled = 1;
	mtx.unlock();

}

void handle_input(int argc, char* argv[],
		char** filename, int* blocksize) {
	int x;
	extern char *optarg;
	extern int optind, optopt, opterr;

	while ((x = getopt(argc, argv, ":f:b:")) != -1) {
		switch (x) {
		case 'f':
			*filename = optarg;
			printf("filename is %s\n", *filename);
			break;
		case 'b':
			*blocksize = atoi(optarg);
			printf("blocksize is %d\n", *blocksize);
			break;
		case ':':
			printf("-%c without parameter\n", optopt);
			break;
		}
	}
}

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

int main(int argc, char ** argv)
{
	char* file = strdup("en_out.txt");
	int blocksize = 8192;
	handle_input(argc, argv, &file, &blocksize);
	FILE *encode_fp = fopen(file, "w");
	server.setup_server(blocksize);  //setting the packet size
	// ------------------------------------------------------------------------------------
	// Step 1: Initialize the OpenCL environment
	// ------------------------------------------------------------------------------------    
	std::cout << "Main program start" << std::endl;
	cl_int err;
	std::string binaryFile = argv[1];
	unsigned fileBufSize;
	std::vector<cl::Device> devices = get_xilinx_devices();
	devices.resize(1);
	cl::Device device = devices[0];
	cl::Context context(device, NULL, NULL, NULL, &err);
	char *fileBuf = read_binary_file(binaryFile, fileBufSize);
	cl::Program::Binaries bins{{fileBuf, fileBufSize}};
	cl::Program program(context, devices, bins, NULL, &err);
	// Disable Profiling to avoid overheads.
	cl::CommandQueue q(context, device, CL_QUEUE_PROFILING_ENABLE, &err);
	cl::Kernel hw_encoding(program, "hardware_encoding", &err);
	// ------------------------------------------------------------------------------------
	// Step 2: Create buffers and initialize test values
	// ------------------------------------------------------------------------------------    	
	std::vector<unsigned char, aligned_allocator<unsigned char>>output_hw(PACKET_SIZE);
	int out_hw_size __attribute__((aligned(sizeof(int))));
	std::vector<unsigned char, aligned_allocator<unsigned char>>chunk_arr(PACKET_SIZE);
	unsigned char in_buf[PACKET_SIZE]; 
	cl::Buffer chunk_arr_cl;
	cl::Buffer output_hw_cl;
	cl::Buffer out_hw_size_cl;
	// ------------------------------------------------------------------------------------
	// Step 2: Create buffers and initialize test values
	// ------------------------------------------------------------------------------------    	
	chunk_arr_cl =   cl::Buffer(context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_ONLY, sizeof(unsigned char) * PACKET_SIZE, chunk_arr.data(), &err);
    output_hw_cl =   cl::Buffer(context, CL_MEM_USE_HOST_PTR  | CL_MEM_WRITE_ONLY, sizeof(unsigned char) * PACKET_SIZE, output_hw.data(), &err);
	out_hw_size_cl = cl::Buffer(context, CL_MEM_USE_HOST_PTR | CL_MEM_WRITE_ONLY, sizeof(int) * 1, &out_hw_size, &err);

	hw_encoding.setArg(0, out_hw_size_cl);
	hw_encoding.setArg(1, output_hw_cl);
	hw_encoding.setArg(2, chunk_arr_cl);
	// ----- -------------------------------------------------------------------------------
	// Step 3: Runnin CDC SHA Dedup before the kernel
	// ------------------------------------------------------------------------------------
	
	/* Currenly we're collecting 8192 bytes of data from the incoming packets and then feeding it into
	 * our pipeline. The packet collection is running on a seperate thread is blocking implementation
	 * for the time being. However, our final design implementation would be non-blocking, running our
	 * compression pipeline parallely with input read and output write on separate threads. */
	total_timer.start();
	do{
		ethernet_timer.start();
		server.get_packet(&input[0]);
		ethernet_timer.stop();

		done = (input[0] & DONE_BIT_L) >> 7;
		length = input[0] | (input[1] << 8);
		length &= ~DONE_BIT_H;
		
	}while(length == 0);
	// counter for packet length 
	packet_counter +=length;
	
	do {
		
		//start packet collection
		std::thread Packet_Collection(&Core2_Process, std::ref(length), std::ref(done), std::ref(input), std::ref(filled));
		//pin the thread to core 2
		pin_thread_to_cpu(Packet_Collection, 2);
		
		if(sha_table.size() > 100000){
          		 // clear the hash table if the size goes above 100,000 chunks 
          		 sha_table.clear();
        	}
		
		if(filled == 1)
		{
			// memcpy in temp buffer
			memset(in_buf, 0, PACKET_SIZE);
			memcpy(in_buf, input+HEADER, PACKET_SIZE);
			mtx.lock();
			filled = 0;
			mtx.unlock();
			cdc_timer.start();
			cdc(in_buf, length, sha_table, encode_fp, done, chunk_arr_cl, output_hw_cl, out_hw_size_cl, output_hw.data(), &out_hw_size, chunk_arr.data(), q, hw_encoding);	
			cdc_timer.stop();
			
		}
       	        		
		Packet_Collection.join();		
	
    	}while(packet_counter < 40600 );
	
	memset(in_buf, 0, PACKET_SIZE);
	memcpy(in_buf, input+HEADER, PACKET_SIZE);
	mtx.lock();
	filled = 0;
	mtx.unlock();
	cdc_timer.start();
	cdc(in_buf, length, sha_table, encode_fp, done, chunk_arr_cl, output_hw_cl, out_hw_size_cl, output_hw.data(), &out_hw_size, chunk_arr.data(), q, hw_encoding);	
	cdc_timer.stop();
	total_timer.stop();
    	/* Finish OpenCL tranfers */
    	q.flush();
    	q.finish();

	fclose(encode_fp);
    
	float cdc_latency = cdc_timer.latency() - (sha_timer.latency() + dedup_timer.latency() + lzw_hw_timer.latency());
	
	std::cout << "--------------- Key Latency ---------------" << std::endl;

   	std::cout << "Total latency of Packet Collection: " << ethernet_timer.latency() << " ms." << std::endl;
	std::cout << "Total latency of CDC + SHA + Dedup + LZW: " << cdc_timer.latency() << " ms." << std::endl;
	std::cout << "Total latency of CDC: " << cdc_latency << " ms." << std::endl;
	std::cout << "Total latency of SHA: " << sha_timer.latency() << " ms." << std::endl;
	std::cout << "Total latency of Dedup: " << dedup_timer.latency() << " ms." << std::endl;
	std::cout << "Total latency of LZW HW: " << lzw_hw_timer.latency() << " ms." << std::endl;
	std::cout << "Total time taken: " << total_timer.latency() << " ms." << std::endl;
	std::cout << "---------------------------------------------------------------" << std::endl;
	std::cout << "Average Latency of Packet Collection: " << ethernet_timer.avg_latency() << " ms." << std::endl;
	std::cout << "Average latency of CDC + SHA + Dedup + LZW: " << cdc_timer.avg_latency() << " ms." << std::endl;
	std::cout << "Average latency of CDC: " << cdc_timer.avg_latency() - (sha_timer.avg_latency() + dedup_timer.avg_latency() + lzw_hw_timer.avg_latency())<< " ms." << std::endl;
	std::cout << "Average latency of SHA: " << sha_timer.avg_latency() << " ms." << std::endl;
	std::cout << "Average latency of Dedup: " << dedup_timer.avg_latency() << " ms." << std::endl;
	std::cout << "Average latency of LZW HW: " << lzw_hw_timer.avg_latency() << " ms." << std::endl;
	std::cout << "Average time taken: " << total_timer.avg_latency() << " ms." << std::endl;
	
	std::cout << "--------------- Data Processed ---------------" << std::endl;
	std::cout << "Total Compressed Data: " << total_lzw_len << " bytes" << std::endl;
	std::cout << "Total Recieved Data: " << packet_counter << " bytes" << std::endl;
	
	//std::cout << "--------------- Key Throughputs ---------------" << std::endl;
	//std::cout << "Throughput of CDC + SHA + DEDUP: " << (packet_counter)/(cdc_timer.latency()*1000) << " Mega Bytes per second" << std::endl;
	//std::cout << "Throughput of CDC: " << packet_counter/(cdc_latency*1000)<< " Mega Bytes per second" << std::endl;
	//std::cout << "Throughput of SHA: " << packet_counter/(sha_timer.latency()*1000) << " Mega Bytes per second" << std::endl;
	//std::cout << "Throughput of Dedup: " << packet_counter/(dedup_timer.latency()*1000) << " Mega Bytes per second" << std::endl;
	//std::cout << "Throughput of LZW HW: " << total_lzw_len/(lzw_hw_timer.latency()*1000) << " Mega Bytes per second" << std::endl;;
	float total_latency = (total_timer.latency() - ethernet_timer.latency()) / 1000.0;
	float total_throughput = (packet_counter * 8 / 1000000.0) / total_latency; // Mb/s
	std::cout << "Input Throughput Encoder: " << total_throughput << " Mb/s."
			<< " (Latency: " << total_latency << "s)." << std::endl;

  return 0; 
}
